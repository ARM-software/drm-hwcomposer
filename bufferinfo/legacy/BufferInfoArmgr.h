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

#ifndef BUFFERINFOARMGR_H_
#define BUFFERINFOARMGR_H_

#include <hardware/gralloc.h>
#include <arm/graphics/privatebuffer/1.0/IAccessor.h>
#include <android/hardware/graphics/common/1.1/types.h>

#include "bufferinfo/BufferInfoGetter.h"

namespace android {

class BufferInfoArmgr : public LegacyBufferInfoGetter {

 public:

  int Init();

  int ConvertBoInfo(buffer_handle_t handle, hwc_drm_bo_t *bo) override;

 private:

  int GetUsage(buffer_handle_t handle, uint32_t *usage);
  int GetFd(buffer_handle_t handle);
  int GetFormat(buffer_handle_t handle, uint32_t *hal_format, uint32_t *format,
                uint64_t *modifiers);
  int GetDimensions(buffer_handle_t handle, uint32_t *width, uint32_t *height);
  int GetPlaneLayout(buffer_handle_t handle, uint32_t *stride_bytes,
                     uint32_t *offsets);

  android::sp<arm::graphics::privatebuffer::V1_0::IAccessor> armgr_acc_;
};
}  // namespace android

#endif
