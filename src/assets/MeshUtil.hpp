#pragma once
#include "MeshAsset.hpp"

namespace s3d {

// Helpers shared by every mesh loader so bounds tracking, normal generation and
// auto-fit behave identically regardless of the source format.
namespace meshutil {

void resetBounds(MeshAsset& asset);

// Expands `asset.boundsMin/Max` to include `p`.
void include(MeshAsset& asset, const Vec3& p);

// Derives center/half/radius and applies the size auto-fit. Returns false when
// the asset has no usable geometry.
bool finalize(MeshAsset& asset);

// Recomputes per-face normals in place (flat shading).
void generateFlatNormals(MeshPart& part);

// Generates flat normals only when the part carries no normals at all.
void generateFlatNormalsIfMissing(MeshPart& part);

} // namespace meshutil
} // namespace s3d