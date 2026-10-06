#include "assets/MeshImport.hpp"

#include "core/Log.hpp"
#include "core/Str.hpp"

namespace s3d {

bool loadMeshFile(const std::string& path, MeshAsset& out) {
    // View-based dispatch: no temporary std::string per call, and this runs for
    // every candidate path resolveMesh() probes.
    const std::string_view ext = str::extensionView(path);
    if (str::equalsIgnoreAscii(ext, ".obj")) return loaders::loadObj(path, out);
    if (str::equalsIgnoreAscii(ext, ".ply")) return loaders::loadPly(path, out);
    if (str::equalsIgnoreAscii(ext, ".fbx")) return loaders::loadFbx(path, out);
    if (str::equalsIgnoreAscii(ext, ".gltf")) return loaders::loadGltf(path, out);
    return loaders::loadGltf(path, out);
}

const std::vector<std::string>& meshExtensions() {
    static const std::vector<std::string> kExts = {".glb", ".gltf", ".obj", ".ply", ".fbx"};
    return kExts;
}

} // namespace s3d