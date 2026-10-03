#include "../MeshImport.hpp"

#include "../../core/Log.hpp"
#include "../MeshUtil.hpp"

// cgltf is a single-header library: exactly one translation unit defines the
// implementation, and it must be this one.
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

namespace s3d {
namespace loaders {
namespace {

Vec3 transformPoint(const float* m, const Vec3& v) {
    return {m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12],
            m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13],
            m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14]};
}

Vec3 transformDirection(const float* m, const Vec3& v) {
    return Vec3{m[0] * v.x + m[4] * v.y + m[8] * v.z,
                m[1] * v.x + m[5] * v.y + m[9] * v.z,
                m[2] * v.x + m[6] * v.y + m[10] * v.z}
        .normalized();
}

void appendPrimitive(cgltf_primitive* prim, const float* world, MeshAsset& asset) {
    if (prim->type != cgltf_primitive_type_triangles) return;

    cgltf_accessor* position = nullptr;
    cgltf_accessor* normal = nullptr;
    for (size_t a = 0; a < prim->attributes_count; ++a) {
        if (prim->attributes[a].type == cgltf_attribute_type_position)
            position = prim->attributes[a].data;
        else if (prim->attributes[a].type == cgltf_attribute_type_normal)
            normal = prim->attributes[a].data;
    }
    if (!position || !position->buffer_view) return;

    MeshPart part;
    if (prim->material && prim->material->has_pbr_metallic_roughness) {
        const float* base = prim->material->pbr_metallic_roughness.base_color_factor;
        part.color[0] = base[0];
        part.color[1] = base[1];
        part.color[2] = base[2];
    }

    const size_t vertexCount = position->count;
    part.vertices.resize(vertexCount * 6);
    const bool hasNormal = normal && normal->buffer_view;

    // Bounds reduce into a local box and fold into the asset once per part.
    meshutil::BoundsBox box;

    float scratch[3];
    for (size_t i = 0; i < vertexCount; ++i) {
        cgltf_accessor_read_float(position, i, scratch, 3);
        const Vec3 worldPos = transformPoint(world, Vec3{scratch[0], scratch[1], scratch[2]});
        part.vertices[i * 6 + 0] = worldPos.x;
        part.vertices[i * 6 + 1] = worldPos.y;
        part.vertices[i * 6 + 2] = worldPos.z;
        box.add(worldPos);
    }
    meshutil::includeBounds(asset, box);
    if (hasNormal) {
        for (size_t i = 0; i < vertexCount; ++i) {
            cgltf_accessor_read_float(normal, i, scratch, 3);
            const Vec3 worldNormal =
                transformDirection(world, Vec3{scratch[0], scratch[1], scratch[2]});
            part.vertices[i * 6 + 3] = worldNormal.x;
            part.vertices[i * 6 + 4] = worldNormal.y;
            part.vertices[i * 6 + 5] = worldNormal.z;
        }
    }

    if (prim->indices) {
        part.indices.resize(prim->indices->count);
        for (size_t i = 0; i < prim->indices->count; ++i)
            part.indices[i] = (unsigned int)cgltf_accessor_read_index(prim->indices, i);
    } else {
        part.indices.resize(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) part.indices[i] = (unsigned int)i;
    }

    meshutil::generateFlatNormalsIfMissing(part);
    asset.parts.push_back(std::move(part));
}

void walkNode(cgltf_node* node, MeshAsset& asset, int depth) {
    if (depth > 32) return;
    float world[16];
    cgltf_node_transform_world(node, world);
    if (node->mesh)
        for (size_t p = 0; p < node->mesh->primitives_count; ++p)
            appendPrimitive(&node->mesh->primitives[p], world, asset);
    for (size_t c = 0; c < node->children_count; ++c)
        walkNode(node->children[c], asset, depth + 1);
}

} // namespace

bool loadGltf(const std::string& path, MeshAsset& out) {
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success) return false;
    if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success) {
        cgltf_free(data);
        return false;
    }

    MeshAsset asset;
    meshutil::resetBounds(asset);
    cgltf_scene* scene = data->scene;
    if (!scene && data->scenes_count > 0) scene = &data->scenes[0];
    if (scene)
        for (size_t i = 0; i < scene->nodes_count; ++i)
            walkNode(scene->nodes[i], asset, 0);
    cgltf_free(data);

    if (asset.parts.empty()) {
        Log::error("glTF: no triangle primitives in %s", path.c_str());
        return false;
    }
    meshutil::finalize(asset);
    out = std::move(asset);
    return true;
}

} // namespace loaders
} // namespace s3d