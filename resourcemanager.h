#ifndef RESOURCEMANAGER_H
#define RESOURCEMANAGER_H

#include "drmresources.h"
#include "platform.h"

namespace android {

class DrmResources;
class Importer;

class ResourceManager {
 public:
  ResourceManager();
  ResourceManager(const ResourceManager &) = delete;
  ResourceManager &operator=(const ResourceManager &) = delete;
  int Init();
  DrmResources *GetDrmResources(int display);
  std::shared_ptr<Importer> GetImporter(int display);
  const gralloc_module_t *GetGralloc();
  DrmConnector *AvailableWritebackConnector(int display);

 private:
  std::vector<std::unique_ptr<DrmResources>> drms_;
  std::vector<std::shared_ptr<Importer>> importers_;
  const gralloc_module_t *gralloc_;
};
}

#endif  // RESOURCEMANAGER_H
