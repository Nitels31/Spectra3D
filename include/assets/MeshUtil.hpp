#pragma once
#include "assets/MeshAsset.hpp"

namespace s3d {

// Helpers shared by every mesh loader so bounds tracking, normal generation and
// auto-fit behave identically regardless of the source format.
namespace meshutil {

// Bounds accumulator. Loaders reduce a whole part into a local box and fold it
// into the asset once, which keeps the asset itself out of the per-vertex loop.
// min/max are exact and order independent, so the folded result is identical to
// calling include() for every vertex.
struct BoundsBox {
    Vec3 lo{1e30f, 1e30f, 1e30f};
    Vec3 hi{-1e30f, -1e30f, -1e30f};

    void add(const Vec3& p) {
        lo.x = std::fmin(lo.x, p.x);
        lo.y = std::fmin(lo.y, p.y);
        lo.z = std::fmin(lo.z, p.z);
        hi.x = std::fmax(hi.x, p.x);
        hi.y = std::fmax(hi.y, p.y);
        hi.z = std::fmax(hi.z, p.z);
    }
};

// Inline so the six fmin/fmax fold into the caller's loop.
inline void resetBounds(MeshAsset& asset) {
    asset.boundsMin = Vec3{1e30f, 1e30f, 1e30f};
    asset.boundsMax = Vec3{-1e30f, -1e30f, -1e30f};
}

// Expands `asset.boundsMin/Max` to include `p`.
inline void include(MeshAsset& asset, const Vec3& p) {
    asset.boundsMin.x = std::fmin(asset.boundsMin.x, p.x);
    asset.boundsMin.y = std::fmin(asset.boundsMin.y, p.y);
    asset.boundsMin.z = std::fmin(asset.boundsMin.z, p.z);
    asset.boundsMax.x = std::fmax(asset.boundsMax.x, p.x);
    asset.boundsMax.y = std::fmax(asset.boundsMax.y, p.y);
    asset.boundsMax.z = std::fmax(asset.boundsMax.z, p.z);
}

// Same as calling include() for every point inside `box`.
inline void includeBounds(MeshAsset& asset, const BoundsBox& box) {
    include(asset, box.lo);
    include(asset, box.hi);
}

// Derives center/half/radius and applies the size auto-fit. Returns false when
// the asset has no usable geometry.
bool finalize(MeshAsset& asset);

// Recomputes per-face normals in place (flat shading).
void generateFlatNormals(MeshPart& part);

// Generates flat normals only when the part carries no normals at all.
void generateFlatNormalsIfMissing(MeshPart& part);

} // namespace meshutil
} // namespace s3d
