#include "MeshRegistry.hpp"

#include "../core/Log.hpp"
#include "../core/Str.hpp"
#include "MeshImport.hpp"

namespace s3d {
namespace {

const std::string kNoName = "-";

} // namespace

int MeshRegistry::importFile(const std::string& path) {
    const std::string name = str::fileStem(path);
    const int existing = find(name);
    if (existing >= 0) return existing;

    MeshAsset asset;
    if (!loadMeshFile(path, asset)) {
        Log::error("Mesh import failed: %s", path.c_str());
        return -1;
    }
    if (asset.parts.empty() || asset.radius < 1e-4f) {
        Log::error("Mesh has no usable geometry: %s", path.c_str());
        return -1;
    }

    asset.name = name;
    Log::info("Imported mesh '%s': %d parts, %d triangles%s", name.c_str(),
              (int)asset.parts.size(), (int)asset.triangleCount(),
              asset.scale != 1.0f ? " (auto-fitted)" : "");
    return importMesh(std::move(asset));
}

int MeshRegistry::importMesh(MeshAsset asset) {
    const std::string name = asset.name.empty() ? kNoName : asset.name;
    const int existing = find(name);
    if (existing >= 0) return existing;

    const int index = (int)assets_.size();
    assets_.push_back(std::move(asset));
    names_.push_back(name);
    byName_[names_.back()] = index;
    return index;
}

int MeshRegistry::find(const std::string& name) const {
    const auto it = byName_.find(name);
    return it == byName_.end() ? -1 : it->second;
}

const MeshAsset* MeshRegistry::at(int index) const {
    if (index < 0 || index >= (int)assets_.size()) return nullptr;
    return &assets_[index];
}

const std::string& MeshRegistry::nameOf(int index) const {
    if (index < 0 || index >= (int)names_.size()) return kNoName;
    return names_[index];
}

} // namespace s3d