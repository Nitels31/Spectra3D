#pragma once
#include <string>
#include <vector>

#include "core/Math.hpp"

namespace s3d {

// Floats per vertex in MeshPart::vertices. Position and normal are always
// there; the texture coordinate always exists too, and is (0,0) on geometry that
// carries none. Sizing it always rather than adding a second optional array
// keeps every vertex the same stride, so the renderer stays one glVertexPointer
// and a mesh without UVs costs two floats per vertex instead of a branch per
// draw call to find out which layout it has.
inline constexpr int kMeshVertexFloats = 8;
inline constexpr int kMeshPositionOffset = 0;
inline constexpr int kMeshNormalOffset = 3;
inline constexpr int kMeshTexCoordOffset = 6;

// One draw call worth of geometry: interleaved position/normal/texcoord (8
// floats per vertex) plus indices, a flat color and an optional texture.
struct MeshPart {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    float color[3] = {1.0f, 1.0f, 1.0f};
    // Index into TextureRegistry, or -1. A part without one draws in its flat
    // colour exactly as before, so adding textures never changes a model that
    // has none.
    int texture = -1;
};

// Imported model plus the derived data the renderer and the editor need.
struct MeshAsset {
    std::string name;
    std::vector<MeshPart> parts;

    // Image files this model referenced, in part order, as paths relative to
    // nothing - already resolved against the model's own directory by the
    // loader. Empty when the model has no textures. Resolution is kept here
    // rather than in the parts because loading is a GL-context operation and
    // must not happen inside a loader: the asset is built without a context and
    // can be serialized to disk, the texture index cannot.
    std::vector<std::string> texturePaths;
    // The same images, index for index, for the ones the model file carried
    // itself. A .glb keeps its textures in its own binary chunk, so there is a
    // matching empty path and these bytes are the only handle there is.
    std::vector<std::vector<unsigned char>> textureBytes;

    Vec3 boundsMin;
    Vec3 boundsMax;
    Vec3 center;
    Vec3 half;
    float radius = 1.0f;
    float scale = 1.0f; // uniform auto-fit factor applied at import time

    unsigned triangleCount() const;
};

} // namespace s3d