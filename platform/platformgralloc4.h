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

#ifndef ANDROID_PLATFORM_GRALLOC4_H_
#define ANDROID_PLATFORM_GRALLOC4_H_

#include <ui/GraphicBufferMapper.h>

#include "drmdevice.h"
#include "platform.h"
#include "platformdrmgeneric.h"

#include <gralloctypes/Gralloc4.h>
#include <android/hardware/graphics/mapper/4.0/IMapper.h>
using android::hardware::graphics::mapper::V4_0::IMapper;
using android::hardware::hidl_vec;

namespace android {

class Gralloc4Importer : public DrmGenericImporter {
 public:
  Gralloc4Importer(DrmDevice *drm);

  int Init();

  int ImportBuffer(buffer_handle_t handle, hwc_drm_bo_t *bo) override;

  bool CanImportBuffer(buffer_handle_t handle) override;

 private:
  static int decodeArmPlaneFds(const hidl_vec<uint8_t>& input, std::vector<int64_t>* fds);

  DrmDevice *drm_;
  bool has_modifier_support_;
  GraphicBufferMapper &mapper_;
  IMapper::MetadataType arm_plane_fds_metadataType_;
};
}  // namespace android

#endif
