#include "../MeshImport.hpp"

#include "../MeshUtil.hpp"

#include <miniply.h>

namespace s3d {
namespace loaders {
namespace {

bool readPlyGeometry(const std::string& path, MeshAsset& asset) {
    miniply::PLYReader reader(path.c_str());
    if (!reader.valid()) return false;

    const uint32_t vertexElementIndex = reader.find_element(miniply::kPLYVertexElement);
    const uint32_t faceElementIndex = reader.find_element(miniply::kPLYFaceElement);
    if (vertexElementIndex == miniply::kInvalidIndex || faceElementIndex == miniply::kInvalidIndex)
        return false;

    miniply::PLYElement* faceElement = reader.get_element(faceElementIndex);
    if (!faceElement) return false;

    uint32_t triangleProp[3];
    if (!faceElement->convert_list_to_fixed_size(faceElement->find_property("vertex_indices"),
                                                 3, triangleProp))
        return false;

    MeshPart part;
    part.color[0] = 0.72f;
    part.color[1] = 0.75f;
    part.color[2] = 0.78f;

    uint32_t vertexCount = 0;
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<unsigned> faceIndices;
    bool hasNormals = false;

    while (reader.has_element()) {
        if (reader.element_is(miniply::kPLYVertexElement)) {
            miniply::PLYElement* vertexElement = reader.get_element(vertexElementIndex);
            if (!vertexElement) {
                reader.next_element();
                continue;
            }
            vertexCount = vertexElement->count;
            if (!vertexCount) return false;

            uint32_t positionProp[3] = {vertexElement->find_property("x"),
                                        vertexElement->find_property("y"),
                                        vertexElement->find_property("z")};
            if (positionProp[0] == miniply::kInvalidIndex ||
                positionProp[1] == miniply::kInvalidIndex ||
                positionProp[2] == miniply::kInvalidIndex)
                return false;

            if (!reader.load_element()) return false;
            positions.resize((size_t)vertexCount * 3);
            reader.extract_properties(positionProp, 3, miniply::PLYPropertyType::Float,
                                      positions.data());

            uint32_t normalProp[3] = {vertexElement->find_property("nx"),
                                      vertexElement->find_property("ny"),
                                      vertexElement->find_property("nz")};
            if (normalProp[0] != miniply::kInvalidIndex &&
                normalProp[1] != miniply::kInvalidIndex &&
                normalProp[2] != miniply::kInvalidIndex) {
                normals.resize((size_t)vertexCount * 3);
                reader.extract_properties(normalProp, 3, miniply::PLYPropertyType::Float,
                                          normals.data());
                hasNormals = true;
            }
        } else if (reader.element_is(miniply::kPLYFaceElement)) {
            if (!reader.load_element()) return false;
            faceIndices.resize((size_t)faceElement->count * 3);
            reader.extract_properties(triangleProp, 3, miniply::PLYPropertyType::UInt,
                                      faceIndices.data());
        }
        reader.next_element();
    }

    if (!vertexCount || faceIndices.empty()) return false;

    part.vertices.resize((size_t)vertexCount * 6);
    for (uint32_t v = 0; v < vertexCount; ++v) {
        const Vec3 p{positions[v * 3 + 0], positions[v * 3 + 1], positions[v * 3 + 2]};
        part.vertices[v * 6 + 0] = p.x;
        part.vertices[v * 6 + 1] = p.y;
        part.vertices[v * 6 + 2] = p.z;
        if (hasNormals) {
            part.vertices[v * 6 + 3] = normals[v * 3 + 0];
            part.vertices[v * 6 + 4] = normals[v * 3 + 1];
            part.vertices[v * 6 + 5] = normals[v * 3 + 2];
        }
        meshutil::include(asset, p);
    }

    part.indices.resize(faceIndices.size());
    for (size_t i = 0; i < faceIndices.size(); ++i)
        part.indices[i] = faceIndices[i] < vertexCount ? faceIndices[i] : 0u;

    meshutil::generateFlatNormalsIfMissing(part);
    asset.parts.push_back(std::move(part));
    return true;
}

} // namespace

bool loadPly(const std::string& path, MeshAsset& out) {
    MeshAsset asset;
    meshutil::resetBounds(asset);
    if (!readPlyGeometry(path, asset)) return false;
    meshutil::finalize(asset);
    out = std::move(asset);
    return true;
}

} // namespace loaders
} // namespace s3d