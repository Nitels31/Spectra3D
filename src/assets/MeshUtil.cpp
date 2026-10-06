#include "assets/MeshUtil.hpp"

#include <algorithm>

namespace s3d {
namespace meshutil {
namespace {

constexpr float kMinRadius = 0.3f; // below this a model is scaled up
constexpr float kMaxRadius = 150.0f; // above this it is scaled down
constexpr float kTargetSmall = 1.5f;
constexpr float kTargetLarge = 100.0f;

} // namespace

bool finalize(MeshAsset& asset) {
    asset.center = (asset.boundsMin + asset.boundsMax) * 0.5f;
    asset.half = (asset.boundsMax - asset.boundsMin) * 0.5f;
    asset.radius = asset.half.length();
    if (asset.radius < 1e-4f) return false;

    float fit = 1.0f;
    if (asset.radius < kMinRadius) fit = kTargetSmall / asset.radius;
    else if (asset.radius > kMaxRadius) fit = kTargetLarge / asset.radius;
    if (fit != 1.0f) {
        asset.scale = fit;
        asset.boundsMin = asset.boundsMin * fit;
        asset.boundsMax = asset.boundsMax * fit;
        asset.center = asset.center * fit;
        asset.half = asset.half * fit;
        asset.radius = asset.radius * fit;
    }
    return asset.radius >= 1e-4f;
}

void generateFlatNormals(MeshPart& part) {
    for (size_t t = 0; t + 2 < part.indices.size(); t += 3) {
        const unsigned a = part.indices[t] * 6;
        const unsigned b = part.indices[t + 1] * 6;
        const unsigned c = part.indices[t + 2] * 6;
        const Vec3 p0{part.vertices[a], part.vertices[a + 1], part.vertices[a + 2]};
        const Vec3 p1{part.vertices[b], part.vertices[b + 1], part.vertices[b + 2]};
        const Vec3 p2{part.vertices[c], part.vertices[c + 1], part.vertices[c + 2]};
        const Vec3 n = cross(p1 - p0, p2 - p0).normalized();
        part.vertices[a + 3] = n.x;
        part.vertices[a + 4] = n.y;
        part.vertices[a + 5] = n.z;
        part.vertices[b + 3] = n.x;
        part.vertices[b + 4] = n.y;
        part.vertices[b + 5] = n.z;
        part.vertices[c + 3] = n.x;
        part.vertices[c + 4] = n.y;
        part.vertices[c + 5] = n.z;
    }
}

void generateFlatNormalsIfMissing(MeshPart& part) {
    for (size_t i = 0; i + 5 < part.vertices.size(); i += 6)
        if (part.vertices[i + 3] == 0.0f && part.vertices[i + 4] == 0.0f &&
            part.vertices[i + 5] == 0.0f) {
            generateFlatNormals(part);
            return;
        }
}

} // namespace meshutil

unsigned MeshAsset::triangleCount() const {
    unsigned tris = 0;
    for (const MeshPart& p : parts) tris += (unsigned)(p.indices.size() / 3);
    return tris;
}

} // namespace s3d