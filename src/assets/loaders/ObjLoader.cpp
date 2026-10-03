#include "../MeshImport.hpp"

#include "../MeshUtil.hpp"

#include <utility>
#include <vector>

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

    // Bounds reduce locally and fold into the asset once per shape.
    meshutil::BoundsBox box;

    // Split by material so each material becomes its own draw call.
    for (auto& shape : result.shapes) {
        const auto& indices = shape.mesh.indices;
        if (indices.empty()) continue;
        const auto& materialIds = shape.mesh.material_ids;

        const auto materialOf = [&](size_t t) {
            return (t / 3 < materialIds.size()) ? materialIds[t / 3] : -1;
        };

        // Material -> part slot, in first-seen order. A flat vector beats the
        // node-based map it replaces: an OBJ has a handful of materials, so the
        // linear probe stays in cache and the lookup is branch-predictable.
        std::vector<std::pair<int, size_t>> slots;
        std::vector<size_t> counts;
        const auto slotOf = [&](int material) {
            for (size_t i = 0; i < slots.size(); ++i)
                if (slots[i].first == material) return i;
            return slots.size();
        };

        // First pass: discover the materials in first-seen order and size every
        // part exactly, so the fill pass never reallocates.
        for (size_t t = 0; t + 2 < indices.size(); t += 3) {
            const int material = materialOf(t);
            const size_t slot = slotOf(material);
            if (slot == slots.size()) {
                MeshPart part;
                if (material >= 0 && material < (int)result.materials.size()) {
                    part.color[0] = result.materials[material].diffuse[0];
                    part.color[1] = result.materials[material].diffuse[1];
                    part.color[2] = result.materials[material].diffuse[2];
                }
                slots.emplace_back(material, asset.parts.size());
                counts.push_back(0);
                asset.parts.push_back(std::move(part));
            }
            counts[slot]++;
        }
        for (size_t slot = 0; slot < slots.size(); ++slot) {
            MeshPart& part = asset.parts[slots[slot].second];
            part.vertices.resize(counts[slot] * 18);
            part.indices.resize(counts[slot] * 3);
        }

        // Second pass: write straight into the sized buffers.
        std::vector<size_t> cursor(slots.size(), 0);
        for (size_t t = 0; t + 2 < indices.size(); t += 3) {
            const size_t slot = slotOf(materialOf(t));
            MeshPart& part = asset.parts[slots[slot].second];

            // `first` is the vertex slot this triangle's corners land in.
            const size_t first = cursor[slot];
            cursor[slot] += 3;

            float* v = part.vertices.data() + first * 6;
            for (int k = 0; k < 3; ++k) {
                const rapidobj::Index& index = indices[t + k];
                const int p = index.position_index * 3;
                const Vec3 worldPos{positions[p + 0], positions[p + 1], positions[p + 2]};
                v[0] = worldPos.x;
                v[1] = worldPos.y;
                v[2] = worldPos.z;
                if (normals && index.normal_index >= 0) {
                    const int n = index.normal_index * 3;
                    v[3] = normals[n + 0];
                    v[4] = normals[n + 1];
                    v[5] = normals[n + 2];
                } else {
                    v[3] = 0.0f;
                    v[4] = 0.0f;
                    v[5] = 0.0f;
                }
                box.add(worldPos);
                v += 6;
            }

            unsigned* idx = part.indices.data() + (t / 3) * 3;
            idx[0] = (unsigned)first;
            idx[1] = (unsigned)first + 1;
            idx[2] = (unsigned)first + 2;
        }
    }

    meshutil::includeBounds(asset, box);

    if (asset.parts.empty()) return false;
    for (MeshPart& part : asset.parts) meshutil::generateFlatNormalsIfMissing(part);
    meshutil::finalize(asset);
    out = std::move(asset);
    return true;
}

} // namespace loaders
} // namespace s3d
