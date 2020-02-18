/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "hwc-platform-gralloc4"

#include "platformgralloc4.h"

#include <android/hardware/graphics/common/1.1/types.h>

#include <drm/drm_fourcc.h>
#include <inttypes.h>
#include <log/log.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "drmdevice.h"
#include "platform.h"

using android::hardware::graphics::common::V1_1::BufferUsage;
using android::hardware::graphics::mapper::V4_0::Error;

namespace android {

Importer *Importer::CreateInstance(DrmDevice *drm) {
  auto importer = std::make_unique<Gralloc4Importer>(drm);
  if (!importer)
    return nullptr;

  int ret = importer->Init();
  if (ret) {
    ALOGE("Failed to initialize the Gralloc 4 importer %d", ret);
    return nullptr;
  }
  return importer.release();
}

Gralloc4Importer::Gralloc4Importer(DrmDevice *drm)
    : DrmGenericImporter(drm),
      drm_(drm),
      has_modifier_support_(false),
      mapper_(GraphicBufferMapper::get()),
      arm_plane_fds_metadataType_({"arm.graphics.ArmMetadataType", 1}) {
}

int Gralloc4Importer::Init() {
  if (mapper_.getMapperVersion() != GraphicBufferMapper::GRALLOC_4) {
    ALOGE("Invalid Gralloc Mapper version");
    return -ENODEV;
  }
  uint64_t cap_value = 0;
  if (drmGetCap(drm_->fd(), DRM_CAP_ADDFB2_MODIFIERS, &cap_value)) {
    ALOGE("drmGetCap failed. Fallback to no modifier support.");
    cap_value = 0;
  }
  has_modifier_support_ = cap_value;

  return 0;
}

int Gralloc4Importer::ImportBuffer(buffer_handle_t handle, hwc_drm_bo_t *bo) {
  if (!handle)
    return -EINVAL;

  *bo = hwc_drm_bo_t{};

  uint64_t usage = 0;
  int err = mapper_.getUsage(handle, &usage);
  if (err) {
    ALOGE("Failed to get usage err=%d", err);
    return err;
  }
  bo->usage = static_cast<uint32_t>(usage);

  android::sp<IMapper> mapper = IMapper::getService();
  void *handle_arg = const_cast<native_handle_t *>(handle);
  std::vector<int64_t> fds;

  mapper->get(handle_arg, arm_plane_fds_metadataType_, [&err, &fds](Error error, const hidl_vec<uint8_t> &metadata)
    {
      switch(error) {
        case Error::NONE:
          break;
        case Error::UNSUPPORTED:
          ALOGE("Gralloc implementation does not support the metadata needed to access the plane fds");
          err = android::BAD_VALUE;
          return;
        default:
          err = android::BAD_VALUE;
          return;
      }
      err = decodeArmPlaneFds(metadata, &fds);
    }
  );
  if (err) {
    return err;
  }
  if (fds.size() < 1) {
    return android::BAD_VALUE;
  }

  std::map<int, uint32_t> imported_fds;
  for (int i = 0; i < fds.size(); ++i) {
    int fd = fds[i];
    if (fd < 0 || static_cast<int64_t>(fd) != fds[i])
    {
      ALOGE("Encountered invalid fd %d", fd);
      return android::BAD_VALUE;
    }
    auto it = imported_fds.find(fd);
    if (it != imported_fds.end()) {
      bo->gem_handles[i] = it->second;
      continue;
    }

    err = drmPrimeFDToHandle(drm_->fd(), fd, &bo->gem_handles[i]);
    if (err) {
      ALOGE("Failed to import prime fd %d ret=%d", fd, err);
      return android::BAD_VALUE;
    }
    imported_fds[fd] = bo->gem_handles[i];
  }

  ui::PixelFormat hal_format;
  err = mapper_.getPixelFormatRequested(handle, &hal_format);
  if (err) {
    ALOGE("Failed to get HAL Pixel Format err=%d", err);
    return err;
  }
  bo->hal_format = static_cast<uint32_t>(hal_format);

  err = mapper_.getPixelFormatFourCC(handle, &bo->format);
  if (err) {
    ALOGE("Failed to get FourCC format err=%d", err);
    return err;
  }

  std::array<uint64_t, 4> modifiers{};
  err = mapper_.getPixelFormatModifier(handle, &modifiers[0]);
  if (err) {
    ALOGE("Failed to get DRM Modifier err=%d", err);
    return err;
  }
  if (!has_modifier_support_ && modifiers[0] != DRM_FORMAT_MOD_INVALID &&
      modifiers[0] != DRM_FORMAT_MOD_LINEAR) {
    ALOGE("No ADDFB2 with modifier support. Can't import modifier %" PRIu64,
          modifiers[0]);
    return -EINVAL;
  }

  uint64_t width = 0;
  err = mapper_.getWidth(handle, &width);
  if (err) {
    ALOGE("Failed to get Width err=%d", err);
    return err;
  }
  bo->width = static_cast<uint32_t>(width);

  uint64_t height = 0;
  err = mapper_.getHeight(handle, &height);
  if (err) {
    ALOGE("Failed to get Height err=%d", err);
    return err;
  }
  bo->height = static_cast<uint32_t>(height);

  std::vector<ui::PlaneLayout> layouts;
  err = mapper_.getPlaneLayouts(handle, &layouts);
  if (err) {
    ALOGE("Failed to get Plane Layouts err=%d", err);
    return err;
  }

  for (uint32_t i = 0; i < layouts.size(); i++) {
    modifiers[i] = modifiers[0];
    bo->pitches[i] = layouts[i].strideInBytes;
    bo->offsets[i] = layouts[i].offsetInBytes;
  }

  if (has_modifier_support_) {
    err = drmModeAddFB2WithModifiers(drm_->fd(), bo->width, bo->height,
                                     bo->format, bo->gem_handles, bo->pitches,
                                     bo->offsets, modifiers.data(), &bo->fb_id,
                                     (modifiers[0] != DRM_FORMAT_MOD_INVALID)
                                         ? DRM_MODE_FB_MODIFIERS
                                         : 0);
  } else {
    err = drmModeAddFB2(drm_->fd(), bo->width, bo->height, bo->format,
                        bo->gem_handles, bo->pitches, bo->offsets, &bo->fb_id,
                        0);
  }

  if (err) {
    ALOGE("Could not create drm fb %d", err);
    return err;
  }

  std::set<uint32_t> unique_gem_handles(std::begin(bo->gem_handles), std::end(bo->gem_handles));
  for (const auto &gem_handle: unique_gem_handles) {
    ImportHandle(gem_handle);
  }
  return 0;
}

bool Gralloc4Importer::CanImportBuffer(buffer_handle_t handle) {
  uint64_t usage = 0;

  int err = mapper_.getUsage(handle, &usage);

  return !err && (usage & BufferUsage::COMPOSER_CLIENT_TARGET);
}

int Gralloc4Importer::decodeArmPlaneFds(const hidl_vec<uint8_t>& input, std::vector<int64_t>* fds) {
  int64_t size = 0;

  memcpy(&size, input.data(), sizeof(int64_t));
  if (size < 0)
  {
    ALOGE("Bad fds size");
    return android::BAD_VALUE;
  }

  fds->resize(size);

  const uint8_t *tmp = input.data() + sizeof(int64_t);
  memcpy(fds->data(), tmp, sizeof(int64_t) * size);

  return android::NO_ERROR;
}

std::unique_ptr<Planner> Planner::CreateInstance(DrmDevice *) {
  auto planner = std::make_unique<Planner>();
  planner->AddStage<PlanStageGreedy>();
  return planner;
}

}  // namespace android
