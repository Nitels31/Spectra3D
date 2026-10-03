#include "../MeshImport.hpp"

#include "../../core/Log.hpp"
#include "../MeshUtil.hpp"

#include <cstring>

#include <ufbx.h>

namespace s3d {
namespace loaders {
namespace {

void transformPoint(const ufbx_matrix* m, double x, double y, double z, float* out) {
    out[0] = (float)(m->m00 * x + m->m01 * y + m->m02 * z + m->m03);
    out[1] = (float)(m->m10 * x + m->m11 * y + m->m12 * z + m->m13);
    out[2] = (float)(m->m20 * x + m->m21 * y + m->m22 * z + m->m23);
}

void transformDirection(const ufbx_matrix* m, double x, double y, double z, float* out) {
    out[0] = (float)(m->m00 * x + m->m01 * y + m->m02 * z);
    out[1] = (float)(m->m10 * x + m->m11 * y + m->m12 * z);
    out[2] = (float)(m->m20 * x + m->m21 * y + m->m22 * z);
    const float len =
        std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    if (len > 1e-9f) {
        out[0] /= len;
        out[1] /= len;
        out[2] /= len;
    }
}

// Scenes imported for previews often contain huge backdrop or helper meshes.
// They are dropped when the model has enough other parts to look sensible.
void dropOutlierParts(MeshAsset& asset) {
    if (asset.parts.size() < 4) return;

    // Bounds have not been finalized yet, so `center` is still the origin -
    // matching the "distance from model origin" heuristic it was written for.
    const Vec3 center = asset.center;
    std::vector<float> extents;
    extents.reserve(asset.parts.size());
    for (const MeshPart& part : asset.parts) {
        float maxExtent = 0.0f;
        for (size_t i = 0; i + 2 < part.vertices.size(); i += 6) {
            const float dx = std::fabs(part.vertices[i] - center.x);
            const float dy = std::fabs(part.vertices[i + 1] - center.y);
            const float dz = std::fabs(part.vertices[i + 2] - center.z);
            maxExtent = std::fmax(maxExtent, std::fmax(dx, std::fmax(dy, dz)));
        }
        extents.push_back(maxExtent);
    }

    std::vector<MeshPart> kept;
    kept.reserve(asset.parts.size());
    int dropped = 0;
    for (size_t i = 0; i < asset.parts.size(); ++i) {
        if (extents[i] > 10.0f) {
            ++dropped;
            continue;
        }
        kept.push_back(std::move(asset.parts[i]));
    }
    if (dropped == 0 || kept.empty()) return;

    Log::info("FBX: dropped %d oversized parts", dropped);
    asset.parts = std::move(kept);
}

void appendMesh(ufbx_node* node, MeshAsset& asset) {
    ufbx_mesh* mesh = node->mesh;
    if (!mesh) return;

    const ufbx_matrix* world = &node->geometry_to_world;
    const ufbx_matrix normalMatrix = ufbx_matrix_for_normals(world);

    MeshPart part;
    part.color[0] = part.color[1] = part.color[2] = 1.0f;
    if (mesh->materials.count > 0) {
        const ufbx_vec3 diffuse = mesh->materials.data[0]->fbx.diffuse_color.value_vec3;
        if (diffuse.x > 0 || diffuse.y > 0 || diffuse.z > 0) {
            part.color[0] = (float)diffuse.x;
            part.color[1] = (float)diffuse.y;
            part.color[2] = (float)diffuse.z;
        }
    }

    const bool hasNormals =
        mesh->vertex_normal.values.data && mesh->vertex_normal.indices.data;

    // FBX meshes index per face corner, so geometry is duplicated per triangle
    // and the result stays a flat, unindexed part.
    for (size_t f = 0; f < mesh->num_faces; ++f) {
        const ufbx_face face = mesh->faces.data[f];
        if (face.num_indices < 3) continue;
        for (size_t k = 1; k + 1 < face.num_indices; ++k) {
            const size_t corners[3] = {face.index_begin, face.index_begin + k,
                                       face.index_begin + k + 1};
            const unsigned base = (unsigned)(part.vertices.size() / 6);
            for (int c = 0; c < 3; ++c) {
                const ufbx_vec3 p = ufbx_get_vertex_vec3(&mesh->vertex_position, corners[c]);
                float worldPos[3];
                transformPoint(world, p.x, p.y, p.z, worldPos);
                part.vertices.push_back(worldPos[0]);
                part.vertices.push_back(worldPos[1]);
                part.vertices.push_back(worldPos[2]);
                if (hasNormals) {
                    const ufbx_vec3 n = ufbx_get_vertex_vec3(&mesh->vertex_normal, corners[c]);
                    float worldNormal[3];
                    transformDirection(&normalMatrix, n.x, n.y, n.z, worldNormal);
                    part.vertices.push_back(worldNormal[0]);
                    part.vertices.push_back(worldNormal[1]);
                    part.vertices.push_back(worldNormal[2]);
                } else {
                    part.vertices.push_back(0.0f);
                    part.vertices.push_back(0.0f);
                    part.vertices.push_back(0.0f);
                }
                meshutil::include(asset, Vec3{worldPos[0], worldPos[1], worldPos[2]});
            }
            part.indices.push_back(base);
            part.indices.push_back(base + 1);
            part.indices.push_back(base + 2);
        }
    }

    if (part.vertices.empty()) return;
    meshutil::generateFlatNormalsIfMissing(part);
    asset.parts.push_back(std::move(part));
}

void walkNode(ufbx_node* node, MeshAsset& asset, int depth) {
    if (depth > 32) return;
    if (node->mesh) appendMesh(node, asset);
    for (size_t i = 0; i < node->children.count; ++i)
        walkNode(node->children.data[i], asset, depth + 1);
}

} // namespace

bool loadFbx(const std::string& path, MeshAsset& out) {
    ufbx_load_opts options;
    memset(&options, 0, sizeof(options));
    options.generate_missing_normals = true;
    options.target_unit_meters = 1.0f;
    options.space_conversion = UFBX_SPACE_CONVERSION_ADJUST_TRANSFORMS;

    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &options, &error);
    if (!scene) {
        Log::error("FBX: %s (%s)", path.c_str(),
                   error.description.data ? error.description.data : "load failed");
        return false;
    }

    MeshAsset asset;
    meshutil::resetBounds(asset);
    for (size_t i = 0; i < scene->nodes.count; ++i) walkNode(scene->nodes.data[i], asset, 0);
    ufbx_free_scene(scene);

    if (asset.parts.empty()) {
        Log::error("FBX: no geometry in %s", path.c_str());
        return false;
    }
    dropOutlierParts(asset);
    meshutil::finalize(asset);
    out = std::move(asset);
    return true;
}

} // namespace loaders
} // namespace s3d