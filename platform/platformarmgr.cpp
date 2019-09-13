/*
 * Copyright (C) 2019 The Android Open Source Project
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

#define LOG_TAG "hwc-platform-armgr"

#include "platformarmgr.h"
#include "drmdevice.h"
#include "platform.h"

#include <drm/drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <cinttypes>

#include <hardware/gralloc.h>
#include <log/log.h>

namespace gc = android::hardware::graphics::common::V1_1;
namespace pb = arm::graphics::privatebuffer::V1_0;

namespace android {

Importer *Importer::CreateInstance(DrmDevice *drm) {
  ArmgrImporter *importer = new ArmgrImporter(drm);
  if (!importer)
    return NULL;

  int ret = importer->Init();
  if (ret) {
    ALOGE("Failed to initialize the Arm Gralloc importer %d", ret);
    delete importer;
    return NULL;
  }
  return importer;
}

ArmgrImporter::ArmgrImporter(DrmDevice *drm)
    : DrmGenericImporter(drm), drm_(drm) {
}

ArmgrImporter::~ArmgrImporter() {
}

int ArmgrImporter::Init() {
  armgr_acc_ = pb::IAccessor::getService();
  if (!armgr_acc_) {
    ALOGE("Failed to get service for IAccessor");
    return -ENODEV;
  }

  return 0;
}

int ArmgrImporter::GetUsage(buffer_handle_t handle, uint32_t *usage) {
  int err = 0;
  armgr_acc_->getUsage(handle,
            [&](const auto &e, const auto &u)
            {
              if (e != pb::Error::NONE) {
                ALOGE("failed to get buffer usage");
                err = -EINVAL;
                return;
              }

              *usage = (uint32_t)u;
            });

  return err;
}

int ArmgrImporter::GetFd(buffer_handle_t handle) {
  int fd;
  armgr_acc_->getAllocation(handle,
          [&](const auto &e, int32_t f, uint32_t size)
          {
            if (e != pb::Error::NONE) {
              ALOGE("failed to get buffer shared fd");
              fd = -EINVAL;
              return;
            }

            UNUSED(size);
            fd = f;
          });

  return fd;
}

int ArmgrImporter::GetFormat(buffer_handle_t handle, uint32_t *hal_format,
                             uint32_t *format, uint64_t *modifiers) {
  int err = 0;
  armgr_acc_->getAllocatedFormat(handle,
          [&](const auto &e, uint32_t fmt, uint64_t mod)
          {
            if (e != pb::Error::NONE) {
              ALOGE("failed to get buffer DRM format");
              err = -EINVAL;
              return;
            }

            *format = fmt;
            *modifiers = mod;
          });

  if (err)
    return err;

  armgr_acc_->getRequestedFormat(handle,
          [&](const auto &e, auto hal_fmt)
          {
            if (e != pb::Error::NONE) {
              ALOGE("failed to get buffer HAL format");
              err = -EINVAL;
              return;
            }

            *hal_format = (uint32_t)hal_fmt;
          });

  return err;
}

int ArmgrImporter::GetDimensions(buffer_handle_t handle, uint32_t *width,
                                 uint32_t *height) {
  int err = 0;
  armgr_acc_->getRequestedDimensions(handle,
          [&](const auto &e, uint32_t w, uint32_t h)
          {
            if (e != pb::Error::NONE) {
              ALOGE("failed to get buffer dimensions");
              err = -EINVAL;
              return;
            }

            *width = w;
            *height = h;
          });

  return err;
}

int ArmgrImporter::GetPlaneLayout(buffer_handle_t handle,
                                  uint32_t *stride_bytes, uint32_t *offsets) {
  int err = 0;
  armgr_acc_->getPlaneLayout(handle,
          [&](const auto &e, const auto &info)
          {
            if (e != pb::Error::NONE) {
              ALOGE("failed to get buffer planes layout");
              err = -EINVAL;
              return;
            }

            for (int i = 0; i < info.size(); ++i) {
              stride_bytes[i] = info[i].byteStride;
              offsets[i] = info[i].offset;
            }

          });

  return err;
}

int ArmgrImporter::ImportBuffer(buffer_handle_t handle, hwc_drm_bo_t *bo) {
  uint64_t modifiers[4] = {0};
  int err = 0, fd;

  if (!handle)
      return -EINVAL;

  memset(bo, 0, sizeof(hwc_drm_bo_t));

  err = GetUsage(handle, &bo->usage);

  // We can't import these types of buffers.
  // These buffers should have been filtered out with CanImportBuffer()
  if (err || !(bo->usage & gc::BufferUsage::COMPOSER_CLIENT_TARGET))
    return -EINVAL;

  fd = GetFd(handle);
  if (fd < 0)
      return fd;

  err = drmPrimeFDToHandle(drm_->fd(), fd, &bo->gem_handles[0]);
  if (err) {
    ALOGE("failed to import prime fd %d ret=%d", fd, err);
    return err;
  }

  err = GetFormat(handle, &bo->hal_format, &bo->format, &modifiers[0]);
  if (err)
      return err;

  err = GetDimensions(handle, &bo->width, &bo->height);
  if (err)
      return err;

  err = GetPlaneLayout(handle, bo->pitches, bo->offsets);
  if (err)
      return err;

  for (int i = 1; i < 4; ++i) {
    if (!bo->pitches[i])
      break;
    bo->gem_handles[i] = bo->gem_handles[0];
  }

  err = drmModeAddFB2WithModifiers(drm_->fd(), bo->width, bo->height,
                                   bo->format, bo->gem_handles, bo->pitches,
                                   bo->offsets, modifiers, &bo->fb_id,
                                   modifiers[0] ? DRM_MODE_FB_MODIFIERS : 0);
  if (err)
    ALOGE("could not create drm fb %d", err);

  return err;
}

bool ArmgrImporter::CanImportBuffer(buffer_handle_t handle) {
  uint32_t usage;
  int err;

  err = GetUsage(handle, &usage);

  return !err && (usage & gc::BufferUsage::COMPOSER_CLIENT_TARGET);
}

class PlanStageArmgr : public Planner::PlanStage {
 public:
  int ProvisionPlanes(std::vector<DrmCompositionPlane> *composition,
                      std::map<size_t, DrmHwcLayer *> &layers, DrmCrtc *crtc,
                      std::vector<DrmPlane *> *planes) {
    int layers_added = 0;

    // Fill up as many DRM planes as we can with buffers that have HW_FB usage.
    // Buffers without HW_FB should have been filtered out with
    // CanImportBuffer(), if we meet one here, just skip it.
    for (auto i = layers.begin(); i != layers.end(); i = layers.erase(i)) {
      if (!(i->second->gralloc_buffer_usage & gc::BufferUsage::COMPOSER_CLIENT_TARGET))
        continue;

      int ret = Emplace(composition, planes, DrmCompositionPlane::Type::kLayer,
                        crtc, std::make_pair(i->first, i->second));
      layers_added++;
      // We don't have any planes left
      if (ret == -ENOENT)
        break;
      else if (ret) {
        ALOGE("Failed to emplace layer %zu, dropping it", i->first);
        return ret;
      }
    }
    // If we didn't emplace anything, return an error to ensure we force client
    // compositing.
    if (!layers_added)
      return -EINVAL;

    return 0;
  }
};

std::unique_ptr<Planner> Planner::CreateInstance(DrmDevice *) {
  auto planner = std::make_unique<Planner>();
  planner->AddStage<PlanStageArmgr>();
  return planner;
}
}  // namespace android
