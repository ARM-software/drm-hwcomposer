#define LOG_TAG "hwc-platform-imagination"

#include "platformimagination.h"
#include <log/log.h>
#include <xf86drm.h>

#include "img_gralloc1_public.h"

namespace android {

Importer *Importer::CreateInstance(DrmDevice *drm) {
  ImaginationImporter *importer = new ImaginationImporter(drm);
  if (!importer)
    return NULL;

  int ret = importer->Init();
  if (ret) {
    ALOGE("Failed to initialize the Imagination importer %d", ret);
    delete importer;
    return NULL;
  }
  return importer;
}

int ImaginationImporter::ImportBuffer(buffer_handle_t handle,
                                      hwc_drm_bo_t *bo) {
  IMG_native_handle_t *hnd = (IMG_native_handle_t *)handle;
  if (!hnd)
    return -EINVAL;

  uint32_t gem_handle;
  int ret = drmPrimeFDToHandle(drm_->fd(), hnd->fd[0], &gem_handle);
  if (ret) {
    ALOGE("failed to import prime fd %d ret=%d", hnd->fd[0], ret);
    return ret;
  }

  /* Extra bits are responsible for buffer compression and memory layout */
  if (hnd->iFormat & ~0x10f) {
    ALOGE("Special buffer formats are not supported");
    return -EINVAL;
  }

  memset(bo, 0, sizeof(hwc_drm_bo_t));
  bo->width = hnd->iWidth;
  bo->height = hnd->iHeight;
  bo->usage = hnd->usage;
  bo->gem_handles[0] = gem_handle;
  bo->pitches[0] = ALIGN(hnd->iWidth, HW_ALIGN) * hnd->uiBpp >> 3;

  switch (hnd->iFormat) {
#ifdef HAL_PIXEL_FORMAT_BGRX_8888
    case HAL_PIXEL_FORMAT_BGRX_8888:
      bo->format = DRM_FORMAT_XRGB8888;
      break;
#endif
    default:
      bo->format = ConvertHalFormatToDrm(hnd->iFormat & 0xf);
      if (bo->format == DRM_FORMAT_INVALID) {
        ALOGE("Cannot convert hal format to drm format %u", hnd->iFormat);
        return -EINVAL;
      }
  }

  ret = drmModeAddFB2(drm_->fd(), bo->width, bo->height, bo->format,
                      bo->gem_handles, bo->pitches, bo->offsets, &bo->fb_id, 0);
  if (ret) {
    ALOGE("could not create drm fb ret: %d", ret);
    return ret;
  }

  ImportHandle(gem_handle);

  return 0;
}

std::unique_ptr<Planner> Planner::CreateInstance(DrmDevice *) {
  std::unique_ptr<Planner> planner(new Planner);
  planner->AddStage<PlanStageGreedy>();
  return planner;
}
}  // namespace android
