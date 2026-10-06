#pragma once
#include <string>
#include <vector>

#include "core/Math.hpp"

namespace s3d {

// One draw call worth of geometry: interleaved position/normal (6 floats per
// vertex) plus indices and a flat color.
struct MeshPart {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    float color[3] = {1.0f, 1.0f, 1.0f};
};

// Imported model plus the derived data the renderer and the editor need.
struct MeshAsset {
    std::string name;
    std::vector<MeshPart> parts;

    Vec3 boundsMin;
    Vec3 boundsMax;
    Vec3 center;
    Vec3 half;
    float radius = 1.0f;
    float scale = 1.0f; // uniform auto-fit factor applied at import time

    unsigned triangleCount() const;
};

} // namespace s3d