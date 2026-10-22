#pragma once
#include <optional>
#include <string>
#include <vector>

#include "assets/CloudSettings.hpp"
#include "assets/LandformSettings.hpp"
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
    // The colour of every vertex, three numbers each, in the same order as the
    // vertices above. Empty for a part that is drawn in its flat colour - which
    // is every imported model, because a file that carries no colours has none
    // to carry.
    //
    // It is a separate array and not four more floats in the vertex, because
    // the vertex is eight floats for every model in the engine and the models
    // are big. The ground is the one thing that is coloured by its vertices,
    // and it can afford an array of its own.
    std::vector<float> colors;
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

    // The settings of a mesh the ENGINE made rather than read from a file.
    //
    // A ground is such a mesh: it is made of noise, and the noise is the same
    // on every machine, so the settings are all that is needed to make it
    // again. They are carried here, with the mesh, because a mesh that says
    // how it was made can be saved and opened - and a mesh that does not is a
    // picture of a mesh: a scene that was saved would come back with a box
    // the size of the ground where the ground was.
    //
    // Empty for a mesh that came from a file: the file is its own recipe, and
    // the mesh is found again by its name.
    std::optional<LandformSettings> landform;

    // The heights of a ground that has been painted, one per vertex, 0..1.
    //
    // The settings are the recipe of the ground, but a ground that has been
    // painted is not the ground the recipe makes - so the heights of the
    // ground are kept beside it, and the ground that is opened is the ground
    // that was painted.
    //
    // Empty for a ground that has not been painted: the recipe is enough.
    std::vector<float> landformHeights;

    // The same for a cloud: a sky the engine made rather than read, and the
    // settings say how it was made. The same seed is the same cloud on every
    // machine, so the settings are all that is needed to make it again.
    std::optional<CloudSettings> cloud;

    Vec3 boundsMin;
    Vec3 boundsMax;
    Vec3 center;
    Vec3 half;
    float radius = 1.0f;
    float scale = 1.0f; // uniform auto-fit factor applied at import time

    unsigned triangleCount() const;
};

} // namespace s3d