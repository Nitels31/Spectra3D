#include "assets/MeshImport.hpp"

#include "assets/MeshUtil.hpp"

#include <utility>
#include <vector>

#if defined(_WIN32)
// rapidobj.hpp includes windows.h and defines both macros unconditionally. MinGW's
// libstdc++ has already defined NOMINMAX as 1, so that redefinition is the one
// warning the build used to print about a header that is not ours. Dropping it
// first makes the two definitions agree, and the macro stays defined either way -
// which is all windows.h looks at when it decides whether to define min/max.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#undef NOMINMAX
#define NOMINMAX
#endif

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
    // OBJ texture references are relative to the .obj itself, so its directory is
    // the base every map_Kd is resolved against.
    const std::string baseDir = std::filesystem::path(path).parent_path().string();

    const float* positions = result.attributes.positions.data();
    const float* normals = result.attributes.normals.data();
    // rapidobj calls these texcoords and stores them as vec2, but reads them
    // through the same attribute accessors as positions. Absent on a model with
    // no vt lines, which is why every use below is guarded.
    const float* texcoords = result.attributes.texcoords.data();

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
                    // map_Kd is the diffuse colour map - the one that actually
                    // shows, so it is the one worth reading. The others (Ka, Ks,
                    // bump) need a different material response to be visible.
                    const std::string& map = result.materials[material].diffuse_texname;
                    if (!map.empty()) {
                        part.texture = (int)asset.texturePaths.size();
                        asset.texturePaths.push_back(
                            baseDir.empty() ? map
                                            : (std::filesystem::path(baseDir) / map).string());
                    }
                }
                slots.emplace_back(material, asset.parts.size());
                counts.push_back(0);
                asset.parts.push_back(std::move(part));
            }
            counts[slot]++;
        }
        for (size_t slot = 0; slot < slots.size(); ++slot) {
            MeshPart& part = asset.parts[slots[slot].second];
            // `counts[slot]` is a count of TRIANGLES, and this loader de-indexes:
            // every triangle contributes its own three corners. So the vertex
            // buffer needs three vertices per count, not one.
            //
            // Sizing it at counts[slot] gave the second pass a buffer a third of the
            // size it was about to write, and the overflow landed on whatever the
            // allocator had handed out next - not a wrong triangle, but a model that
            // sits at the origin, vanishes as the camera nears it, and whose
            // bounding box belongs to someone else's memory. The index buffer below
            // is sized off the same count and was already right, which is why the
            // two disagreed.
            part.vertices.resize(counts[slot] * 3 * kMeshVertexFloats);
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

            float* v = part.vertices.data() + first * kMeshVertexFloats;
            for (int k = 0; k < 3; ++k) {
                const rapidobj::Index& index = indices[t + k];
                const int p = index.position_index * 3;
                const Vec3 worldPos{positions[p + 0], positions[p + 1], positions[p + 2]};
                v[0] = worldPos.x;
                v[1] = worldPos.y;
                v[2] = worldPos.z;
                if (normals && index.normal_index >= 0) {
                    const int n = index.normal_index * 3;
                    v[kMeshNormalOffset + 0] = normals[n + 0];
                    v[kMeshNormalOffset + 1] = normals[n + 1];
                    v[kMeshNormalOffset + 2] = normals[n + 2];
                } else {
                    v[kMeshNormalOffset + 0] = 0.0f;
                    v[kMeshNormalOffset + 1] = 0.0f;
                    v[kMeshNormalOffset + 2] = 0.0f;
                }
                // (0,0) when the model has no vt, or this corner references one
                // that does not exist. A vertex without UVs gets every texel from
                // one corner of the image, which is the same as having no texture
                // bound - so a model that is not textured yet draws exactly as it
                // did before rather than picking up a colour out of the map.
                if (texcoords && index.texcoord_index >= 0) {
                    const int t2 = index.texcoord_index * 2;
                    v[kMeshTexCoordOffset + 0] = texcoords[t2 + 0];
                    v[kMeshTexCoordOffset + 1] = texcoords[t2 + 1];
                } else {
                    v[kMeshTexCoordOffset + 0] = 0.0f;
                    v[kMeshTexCoordOffset + 1] = 0.0f;
                }
                box.add(worldPos);
                v += kMeshVertexFloats;
            }

            // `first` is this triangle's slot inside ITS OWN part, and it advances by three
            // per triangle, so it is already the index-buffer offset for this part.
            //
            // Not the loop counter `t`: that counts triangles across the whole file,
            // across every material and every shape, while part.indices only holds
            // the triangles of one material. An OBJ with a second material writes
            // its second part's indices past the end of that part's buffer and
            // corrupts the heap next to it - which is not a wrong triangle, it is a
            // model that renders at the origin, vanishes when the camera nears it,
            // and whose bounding box belongs to whatever the allocator handed out.
            unsigned* idx = part.indices.data() + first;
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
