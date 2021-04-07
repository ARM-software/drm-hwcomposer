/*
 * Copyright (C) 2021 The Android Open Source Project
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

#define LOG_TAG "hwc-bufferinfo-armgr"

#include <drm/drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <cinttypes>

#include <hardware/gralloc.h>
#include <log/log.h>

#include "BufferInfoArmgr.h"

#define UNUSED(x) (void)(x)

namespace pb = arm::graphics::privatebuffer::V1_0;

namespace android {

LEGACY_BUFFER_INFO_GETTER(BufferInfoArmgr);

int BufferInfoArmgr::Init() {
  armgr_acc_ = pb::IAccessor::getService();
  if (!armgr_acc_) {
    ALOGE("Failed to get service for IAccessor");
    return -ENODEV;
  }

  return 0;
}

int BufferInfoArmgr::GetUsage(buffer_handle_t handle, uint32_t *usage) {
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

int BufferInfoArmgr::GetFd(buffer_handle_t handle) {
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

int BufferInfoArmgr::GetFormat(buffer_handle_t handle, uint32_t *hal_format,
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

int BufferInfoArmgr::GetDimensions(buffer_handle_t handle, uint32_t *width,
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

int BufferInfoArmgr::GetPlaneLayout(buffer_handle_t handle,
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

int BufferInfoArmgr::ConvertBoInfo(buffer_handle_t handle, hwc_drm_bo_t *bo) {
  int err = 0;

  memset(bo, 0, sizeof(hwc_drm_bo_t));

  err = GetDimensions(handle, &bo->width, &bo->height);
  if (err)
      return err;

  err = GetFormat(handle, &bo->hal_format, &bo->format, bo->modifiers);
  if (err)
      return err;

  err = GetUsage(handle, &bo->usage);
  if (err)
    return err;

  err = GetPlaneLayout(handle, bo->pitches, bo->offsets);
  if (err)
      return err;

  bo->prime_fds[0] = GetFd(handle);

  for (int i = 1; i < HWC_DRM_BO_MAX_PLANES; i++) {
    if (bo->pitches[i])
      bo->prime_fds[i] = bo->prime_fds[0];
  }

  return 0;
}

}  // namespace android
