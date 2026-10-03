#include "MeshImport.hpp"

#include "../core/Log.hpp"
#include "../core/Str.hpp"

namespace s3d {

bool loadMeshFile(const std::string& path, MeshAsset& out) {
    const std::string ext = str::extension(path);
    if (ext == ".obj") return loaders::loadObj(path, out);
    if (ext == ".ply") return loaders::loadPly(path, out);
    if (ext == ".fbx") return loaders::loadFbx(path, out);
    if (ext == ".gltf") return loaders::loadGltf(path, out);
    return loaders::loadGltf(path, out);
}

const std::vector<std::string>& meshExtensions() {
    static const std::vector<std::string> kExts = {".glb", ".gltf", ".obj", ".ply", ".fbx"};
    return kExts;
}

} // namespace s3d