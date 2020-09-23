/*
 * Copyright (C) 2015 The Android Open Source Project
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

#define LOG_TAG "hwc-platform-drm-generic"

#include "platformdrmgeneric.h"

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cutils/properties.h>
#include <gralloc_handle.h>
#include <hardware/gralloc.h>
#include <log/log.h>

namespace android {

#ifdef USE_DRM_GENERIC_IMPORTER
// static
Importer *Importer::CreateInstance(DrmDevice *drm) {
  DrmGenericImporter *importer = new DrmGenericImporter(drm);
  if (!importer)
    return NULL;

  int ret = importer->Init();
  if (ret) {
    ALOGE("Failed to initialize the nv importer %d", ret);
    delete importer;
    return NULL;
  }
  return importer;
}
#endif

DrmGenericImporter::DrmGenericImporter(DrmDevice *drm) : drm_(drm) {
}

DrmGenericImporter::~DrmGenericImporter() {
}

int DrmGenericImporter::Init() {
  int ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                          (const hw_module_t **)&gralloc_);
  if (ret) {
    ALOGE("Failed to open gralloc module");
    return ret;
  }

  ALOGI("Using %s gralloc module: %s\n", gralloc_->common.name,
        gralloc_->common.author);

  return 0;
}

enum chroma_order {
  YCbCr,
  YCrCb,
};

struct droid_yuv_format {
  /* Lookup keys */
  int native;                     /* HAL_PIXEL_FORMAT_ */
  enum chroma_order chroma_order; /* chroma order is {Cb, Cr} or {Cr, Cb} */
  int chroma_step; /* Distance in bytes between subsequent chroma pixels. */

  /* Result */
  int fourcc; /* DRM_FORMAT_ */
};

/* The following table is used to look up a DRI image FourCC based
 * on native format and information contained in android_ycbcr struct. */
static const struct droid_yuv_format droid_yuv_formats[] = {
    /* Native format, YCrCb, Chroma step, DRI image FourCC */
    {HAL_PIXEL_FORMAT_YCbCr_420_888, YCbCr, 2, DRM_FORMAT_NV12},
    {HAL_PIXEL_FORMAT_YCbCr_420_888, YCbCr, 1, DRM_FORMAT_YUV420},
    {HAL_PIXEL_FORMAT_YCbCr_420_888, YCrCb, 1, DRM_FORMAT_YVU420},
    {HAL_PIXEL_FORMAT_YV12, YCrCb, 1, DRM_FORMAT_YVU420},
    /* HACK: See droid_create_image_from_prime_fds() and
     * https://issuetracker.google.com/32077885. */
    {HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, YCbCr, 2, DRM_FORMAT_NV12},
    {HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, YCbCr, 1, DRM_FORMAT_YUV420},
    {HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, YCrCb, 1, DRM_FORMAT_YVU420},
    {HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, YCrCb, 1, DRM_FORMAT_AYUV},
    {HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, YCrCb, 1, DRM_FORMAT_XYUV8888},
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static int get_fourcc_yuv(int native, enum chroma_order chroma_order,
                          int chroma_step) {
  for (int i = 0; i < ARRAY_SIZE(droid_yuv_formats); ++i)
    if (droid_yuv_formats[i].native == native &&
        droid_yuv_formats[i].chroma_order == chroma_order &&
        droid_yuv_formats[i].chroma_step == chroma_step)
      return droid_yuv_formats[i].fourcc;

  return -1;
}

static bool is_yuv(int native) {
  for (int i = 0; i < ARRAY_SIZE(droid_yuv_formats); ++i)
    if (droid_yuv_formats[i].native == native)
      return true;

  return false;
}

bool DrmGenericImporter::GetYuvPlaneInfo(int num_fds, buffer_handle_t handle,
                                         hwc_drm_bo_t *bo) {
  struct android_ycbcr ycbcr;
  enum chroma_order chroma_order;
  int ret;

  if (!gralloc_->lock_ycbcr) {
    static std::once_flag once;
    std::call_once(once,
                   []() { ALOGW("Gralloc does not support lock_ycbcr()"); });
    return false;
  }

  memset(&ycbcr, 0, sizeof(ycbcr));
  ret = gralloc_->lock_ycbcr(gralloc_, handle, 0, 0, 0, 0, 0, &ycbcr);
  if (ret) {
    ALOGW("gralloc->lock_ycbcr failed: %d", ret);
    return false;
  }
  gralloc_->unlock(gralloc_, handle);

  /* When lock_ycbcr's usage argument contains no SW_READ/WRITE flags
   * it will return the .y/.cb/.cr pointers based on a NULL pointer,
   * so they can be interpreted as offsets. */
  bo->offsets[0] = (size_t)ycbcr.y;
  /* We assume here that all the planes are located in one DMA-buf. */
  if ((size_t)ycbcr.cr < (size_t)ycbcr.cb) {
    chroma_order = YCrCb;
    bo->offsets[1] = (size_t)ycbcr.cr;
    bo->offsets[2] = (size_t)ycbcr.cb;
  } else {
    chroma_order = YCbCr;
    bo->offsets[1] = (size_t)ycbcr.cb;
    bo->offsets[2] = (size_t)ycbcr.cr;
  }

  /* .ystride is the line length (in bytes) of the Y plane,
   * .cstride is the line length (in bytes) of any of the remaining
   * Cb/Cr/CbCr planes, assumed to be the same for Cb and Cr for fully
   * planar formats. */
  bo->pitches[0] = ycbcr.ystride;
  bo->pitches[1] = bo->pitches[2] = ycbcr.cstride;

  /* .chroma_step is the byte distance between the same chroma channel
   * values of subsequent pixels, assumed to be the same for Cb and Cr. */
  bo->format = get_fourcc_yuv(bo->hal_format, chroma_order, ycbcr.chroma_step);
  if (bo->format == -1) {
    ALOGW(
        "unsupported YUV format, native = %x, chroma_order = %s, chroma_step = "
        "%d",
        bo->hal_format, chroma_order == YCbCr ? "YCbCr" : "YCrCb",
        (int)ycbcr.chroma_step);
    return false;
  }

  /*
   * Since this is EGL_NATIVE_BUFFER_ANDROID don't assume that
   * the single-fd case cannot happen.  So handle eithe single
   * fd or fd-per-plane case:
   */
  if (num_fds == 1) {
    bo->prime_fds[2] = bo->prime_fds[1] = bo->prime_fds[0];
  } else {
    int expected_planes = (ycbcr.chroma_step == 2) ? 2 : 3;
    if (num_fds != expected_planes)
      return false;
  }

  return true;
}

uint32_t DrmGenericImporter::ConvertHalFormatToDrm(uint32_t hal_format) {
  switch (hal_format) {
    case HAL_PIXEL_FORMAT_RGB_888:
      return DRM_FORMAT_BGR888;
    case HAL_PIXEL_FORMAT_BGRA_8888:
      return DRM_FORMAT_ARGB8888;
    case HAL_PIXEL_FORMAT_RGBX_8888:
      return DRM_FORMAT_XBGR8888;
    case HAL_PIXEL_FORMAT_RGBA_8888:
      return DRM_FORMAT_ABGR8888;
    case HAL_PIXEL_FORMAT_RGB_565:
      return DRM_FORMAT_BGR565;
    case HAL_PIXEL_FORMAT_YV12:
      return DRM_FORMAT_YVU420;
    default:
      ALOGE("Cannot convert hal format to drm format %u", hal_format);
      return DRM_FORMAT_INVALID;
  }
}

uint32_t DrmGenericImporter::DrmFormatToBitsPerPixel(uint32_t drm_format) {
  switch (drm_format) {
    case DRM_FORMAT_ARGB8888:
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_ABGR8888:
      return 32;
    case DRM_FORMAT_BGR888:
      return 24;
    case DRM_FORMAT_BGR565:
      return 16;
    case DRM_FORMAT_YVU420:
      return 12;
    default:
      ALOGE("Cannot convert hal format %u to bpp (returning 32)", drm_format);
      return 32;
  }
}

int DrmGenericImporter::ConvertBoInfo(buffer_handle_t handle,
                                      hwc_drm_bo_t *bo) {
  gralloc_handle_t *gr_handle = gralloc_handle(handle);
  if (!gr_handle)
    return -EINVAL;

  bo->width = gr_handle->width;
  bo->height = gr_handle->height;
  bo->hal_format = gr_handle->format;

#if GRALLOC_HANDLE_VERSION < 4
  static std::once_flag once;
  std::call_once(once, []() {
    ALOGE(
        "libdrm < v2.4.97 has broken gralloc_handle structure. Please update.");
  });
#endif
#if GRALLOC_HANDLE_VERSION == 4
  bo->modifiers[0] = gr_handle->modifier;
  bo->with_modifiers = gr_handle->modifier != DRM_FORMAT_MOD_NONE &&
                       gr_handle->modifier != DRM_FORMAT_MOD_INVALID;
#endif

  bo->usage = gr_handle->usage;
  bo->prime_fds[0] = gr_handle->prime_fd;

  if (is_yuv(gr_handle->format)) {
    if (!GetYuvPlaneInfo(handle->numFds, handle, bo))
      return -EINVAL;
  } else {
    bo->pitches[0] = gr_handle->stride;
    bo->offsets[0] = 0;
    bo->format = ConvertHalFormatToDrm(gr_handle->format);
    if (bo->format == DRM_FORMAT_INVALID)
      return -EINVAL;
  }

  bo->pixel_stride = (gr_handle->stride * 8) /
                     DrmFormatToBitsPerPixel(bo->format);

  return 0;
}

int DrmGenericImporter::ImportBuffer(buffer_handle_t handle, hwc_drm_bo_t *bo) {
  memset(bo, 0, sizeof(hwc_drm_bo_t));

  int ret = ConvertBoInfo(handle, bo);
  if (ret)
    return ret;

  ret = drmPrimeFDToHandle(drm_->fd(), bo->prime_fds[0], &bo->gem_handles[0]);
  if (ret) {
    ALOGE("failed to import prime fd %d ret=%d", bo->prime_fds[0], ret);
    return ret;
  }

  for (int i = 1; i < HWC_DRM_BO_MAX_PLANES; i++) {
    int fd = bo->prime_fds[i];
    if (fd != 0) {
      if (fd != bo->prime_fds[0]) {
        ALOGE("Multiplanar FBs are not supported by this version of composer");
        return -ENOTSUP;
      }
      bo->gem_handles[i] = bo->gem_handles[0];
    }
  }

  if (!bo->with_modifiers)
    ret = drmModeAddFB2(drm_->fd(), bo->width, bo->height, bo->format,
                        bo->gem_handles, bo->pitches, bo->offsets, &bo->fb_id,
                        0);
  else
    ret = drmModeAddFB2WithModifiers(drm_->fd(), bo->width, bo->height,
                                     bo->format, bo->gem_handles, bo->pitches,
                                     bo->offsets, bo->modifiers, &bo->fb_id,
                                     bo->modifiers[0] ? DRM_MODE_FB_MODIFIERS
                                                      : 0);

  if (ret) {
    ALOGE("could not create drm fb %d", ret);
    return ret;
  }

  ImportHandle(bo->gem_handles[0]);

  return ret;
}

int DrmGenericImporter::ReleaseBuffer(hwc_drm_bo_t *bo) {
  if (bo->fb_id)
    if (drmModeRmFB(drm_->fd(), bo->fb_id))
      ALOGE("Failed to rm fb");

  for (int i = 0; i < HWC_DRM_BO_MAX_PLANES; i++) {
    if (!bo->gem_handles[i])
      continue;

    if (ReleaseHandle(bo->gem_handles[i])) {
      ALOGE("Failed to release gem handle %d", bo->gem_handles[i]);
    } else {
      for (int j = i + 1; j < HWC_DRM_BO_MAX_PLANES; j++)
        if (bo->gem_handles[j] == bo->gem_handles[i])
          bo->gem_handles[j] = 0;
      bo->gem_handles[i] = 0;
    }
  }
  return 0;
}

bool DrmGenericImporter::CanImportBuffer(buffer_handle_t handle) {
  hwc_drm_bo_t bo;

  int ret = ConvertBoInfo(handle, &bo);
  if (ret)
    return false;

  if (bo.prime_fds[0] == 0)
    return false;

  return true;
}

std::unique_ptr<Planner> Planner::CreateInstance(DrmDevice *) {
  std::unique_ptr<Planner> planner(new Planner);
  planner->AddStage<PlanStageGreedy>();
  return planner;
}

int DrmGenericImporter::ImportHandle(uint32_t gem_handle) {
  gem_refcount_[gem_handle]++;

  return 0;
}

int DrmGenericImporter::ReleaseHandle(uint32_t gem_handle) {
  if (--gem_refcount_[gem_handle])
    return 0;

  gem_refcount_.erase(gem_handle);

  return CloseHandle(gem_handle);
}

int DrmGenericImporter::CloseHandle(uint32_t gem_handle) {
  struct drm_gem_close gem_close;

  memset(&gem_close, 0, sizeof(gem_close));

  gem_close.handle = gem_handle;
  int ret = drmIoctl(drm_->fd(), DRM_IOCTL_GEM_CLOSE, &gem_close);
  if (ret)
    ALOGE("Failed to close gem handle %d %d", gem_handle, ret);

  return ret;
}
}
