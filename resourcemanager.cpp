#include "resourcemanager.h"
#include <cutils/log.h>
#include <cutils/properties.h>

namespace android {

ResourceManager::ResourceManager() : gralloc_(NULL) {
}

int ResourceManager::Init() {
  char path_pattern[PROPERTY_VALUE_MAX];
  property_get("hwc.drm.device", path_pattern, "/dev/dri/card%");

  uint8_t minor = 0;
  int last_display_index = 0;
  int last_char = strlen(path_pattern) - 1;
  do {
    char path[PROPERTY_VALUE_MAX];
    if (path_pattern[last_char] == '%') {
      path_pattern[last_char] = '\0';
      snprintf(path, PROPERTY_VALUE_MAX, "%s%d", path_pattern, minor);
      path_pattern[last_char] = '%';
    } else {
      snprintf(path, PROPERTY_VALUE_MAX, "%s", path_pattern);
    }
    std::unique_ptr<DrmResources> drm = std::make_unique<DrmResources>();
    last_display_index = drm->Init(this, path, last_display_index);
    if (last_display_index < 0) {
      break;
    }
    std::shared_ptr<Importer> importer;
    importer.reset(Importer::CreateInstance(drm.get()));
    if (!importer) {
      ALOGE("Failed to create importer instance");
      break;
    }
    importers_.push_back(importer);
    drms_.push_back(std::move(drm));
    minor++;
    last_display_index++;
  } while (path_pattern[last_char] == '%');

  if (!drms_.size()) {
    ALOGE("Failed to find any working drm device");
    return -EINVAL;
  }

  return hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                       (const hw_module_t **)&gralloc_);
}

DrmConnector *ResourceManager::AvailableWritebackConnector(int display) {
  DrmResources *drm_resource = GetDrmResources(display);
  DrmConnector *writeback_conn = NULL;
  if (drm_resource) {
    writeback_conn = drm_resource->AvailableWritebackConnector(display);
    if (writeback_conn) {
      ALOGI("Use writeback connected to display %d\n",
            writeback_conn->display());
      return writeback_conn;
    }
  }
  for (auto &drm : drms_) {
    if (drm.get() == drm_resource)
      continue;
    writeback_conn = drm->AvailableWritebackConnector(display);
    if (writeback_conn) {
      ALOGI("Use writeback connected to display %d\n",
            writeback_conn->display());
      return writeback_conn;
    }
  }
  return writeback_conn;
}

DrmResources *ResourceManager::GetDrmResources(int display) {
  for (uint32_t i = 0; i < drms_.size(); i++) {
    if (drms_[i]->HandlesDisplay(display))
      return drms_[i].get();
  }
  return NULL;
}

std::shared_ptr<Importer> ResourceManager::GetImporter(int display) {
  for (uint32_t i = 0; i < drms_.size(); i++) {
    if (drms_[i]->HandlesDisplay(display))
      return importers_[i];
  }
  return NULL;
}

const gralloc_module_t *ResourceManager::GetGralloc() {
  return gralloc_;
}
}
