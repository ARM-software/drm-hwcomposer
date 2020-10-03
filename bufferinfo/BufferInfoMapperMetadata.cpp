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

#if PLATFORM_SDK_VERSION >= 30

#define LOG_TAG "hwc-bufferinfo-mappermetadata"

#include "BufferInfoMapperMetadata.h"

#include <drm/drm_fourcc.h>
#include <inttypes.h>
#include <log/log.h>
#include <ui/GraphicBufferMapper.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

using android::hardware::graphics::common::V1_1::BufferUsage;

namespace android {

BufferInfoGetter *BufferInfoMapperMetadata::CreateInstance() {
  if (GraphicBufferMapper::getInstance().getMapperVersion() <
      GraphicBufferMapper::GRALLOC_4)
    return nullptr;

  return new BufferInfoMapperMetadata();
}

int BufferInfoMapperMetadata::ConvertBoInfo(buffer_handle_t /*handle*/,
                                            hwc_drm_bo_t * /*bo*/) {
  return -EINVAL;
}

}  // namespace android

#endif
