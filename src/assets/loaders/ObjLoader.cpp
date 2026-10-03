#include "../MeshImport.hpp"

#include "../MeshUtil.hpp"

#include <map>

#include <rapidobj.hpp>

namespace s3d {
namespace loaders {

bool loadObj(const std::string& path, MeshAsset& out) {
    rapidobj::Result result = rapidobj::ParseFile(std::filesystem::path(path));
    if (result.error) {
        // Missing or unreadable .mtl should not block the geometry import.
        result = rapidobj::ParseFile(std::filesystem::path(path),
                                     rapidobj::MaterialLibrary::Ignore());
    }
    if (result.error) return false;
    if (!rapidobj::Triangulate(result)) return false;

    MeshAsset asset;
    meshutil::resetBounds(asset);

    const float* positions = result.attributes.positions.data();
    const float* normals = result.attributes.normals.data();

    // Split by material so each material becomes its own draw call.
    for (auto& shape : result.shapes) {
        const auto& indices = shape.mesh.indices;
        if (indices.empty()) continue;
        std::map<int, size_t> partByMaterial;
        for (size_t t = 0; t + 2 < indices.size(); t += 3) {
            const int material =
                (t / 3 < shape.mesh.material_ids.size()) ? shape.mesh.material_ids[t / 3] : -1;
            auto it = partByMaterial.find(material);
            if (it == partByMaterial.end()) {
                MeshPart part;
                if (material >= 0 && material < (int)result.materials.size()) {
                    part.color[0] = result.materials[material].diffuse[0];
                    part.color[1] = result.materials[material].diffuse[1];
                    part.color[2] = result.materials[material].diffuse[2];
                }
                partByMaterial[material] = asset.parts.size();
                asset.parts.push_back(std::move(part));
                it = partByMaterial.find(material);
            }
            MeshPart& part = asset.parts[it->second];
            const unsigned base = (unsigned)(part.vertices.size() / 6);
            for (int k = 0; k < 3; ++k) {
                const rapidobj::Index& index = indices[t + k];
                const int p = index.position_index * 3;
                const Vec3 worldPos{positions[p + 0], positions[p + 1], positions[p + 2]};
                part.vertices.push_back(worldPos.x);
                part.vertices.push_back(worldPos.y);
                part.vertices.push_back(worldPos.z);
                if (normals && index.normal_index >= 0) {
                    const int n = index.normal_index * 3;
                    part.vertices.push_back(normals[n + 0]);
                    part.vertices.push_back(normals[n + 1]);
                    part.vertices.push_back(normals[n + 2]);
                } else {
                    part.vertices.push_back(0.0f);
                    part.vertices.push_back(0.0f);
                    part.vertices.push_back(0.0f);
                }
                meshutil::include(asset, worldPos);
                part.indices.push_back(base + (unsigned)k);
            }
        }
    }

    if (asset.parts.empty()) return false;
    for (MeshPart& part : asset.parts) meshutil::generateFlatNormalsIfMissing(part);
    meshutil::finalize(asset);
    out = std::move(asset);
    return true;
}

} // namespace loaders
} // namespace s3d