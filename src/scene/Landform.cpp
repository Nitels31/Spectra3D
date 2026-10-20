#include "scene/Landform.hpp"

#include "core/Math.hpp"
#include "scene/Noise.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

namespace s3d {
namespace {

// The noise is sampled on the ground's own scale, so the shape of a landform
// does not change when the landform is made bigger: a hill is a hill, it is
// just further away.
constexpr float kNoiseScale = 0.055f;

// The number of squares a landform may be cut into. A hundred thousand squares
// is a mesh nobody can look at and a machine nobody can afford.
constexpr int kMinCells = 2;
constexpr int kMaxCells = 512;

// Runs `body(row)` for every row, on as many threads as there are rows to
// share. The rows are independent, so nothing is shared and nothing is locked.
template <typename Fn>
void forEachRow(int rows, int threads, Fn body) {
    const int workers = std::clamp(threads, 1, 6);
    if (workers <= 1 || rows <= 1) {
        for (int row = 0; row < rows; ++row) body(row);
        return;
    }

    std::atomic<int> next{0};
    std::vector<std::thread> pool;
    pool.reserve((size_t)workers);
    for (int w = 0; w < workers; ++w) {
        pool.emplace_back([&] {
            for (int row = next.fetch_add(1); row < rows; row = next.fetch_add(1)) body(row);
        });
    }
    for (std::thread& t : pool) t.join();
}

} // namespace

MeshAsset generateLandform(const LandformSettings& settings) {
    const int cells = std::clamp(settings.cells, kMinCells, kMaxCells);
    const int side = cells + 1;  // the vertices across and down
    const float stepX = settings.width / (float)cells;
    const float stepZ = settings.depth / (float)cells;
    const float halfWidth = settings.width * 0.5f;
    const float halfDepth = settings.depth * 0.5f;
    const float span = settings.maxHeight - settings.minHeight;

    // The heights first, one per vertex, in rows. A row's squares do not depend
    // on the next row, so the rows are made at the same time.
    std::vector<float> heights((size_t)side * (size_t)side, settings.minHeight);
    forEachRow(side, settings.threads, [&](int row) {
        for (int col = 0; col < side; ++col) {
            const float x = -halfWidth + (float)col * stepX;
            const float z = -halfDepth + (float)row * stepZ;
            const float n = fbm(x * kNoiseScale, z * kNoiseScale, settings.seed, 4);
            heights[(size_t)row * (size_t)side + (size_t)col] = settings.minHeight + n * span;
        }
    });

    // Then the vertices and their normals, also in rows. The normal of a square
    // comes from its neighbours, and the neighbours of a row are in the rows
    // above and below - which are made already.
    MeshAsset asset;
    asset.name = "landform";
    MeshPart part;
    // Every row writes its own stretch of the vertices, so the rows can be made
    // at the same time and still come out in the order the indices expect.
    part.vertices.assign((size_t)side * (size_t)side * kMeshVertexFloats, 0.0f);
    part.indices.reserve((size_t)cells * (size_t)cells * 6);

    forEachRow(side, settings.threads, [&](int row) {
        for (int col = 0; col < side; ++col) {
            const float x = -halfWidth + (float)col * stepX;
            const float z = -halfDepth + (float)row * stepZ;
            const float y = heights[(size_t)row * (size_t)side + (size_t)col];

            // The normal comes from the slope: the height a step to the left
            // and a step to the front.
            const int left = std::max(0, col - 1);
            const int right = std::min(side - 1, col + 1);
            const int up = std::max(0, row - 1);
            const int down = std::min(side - 1, row + 1);
            const float dX = heights[(size_t)row * (size_t)side + (size_t)right] -
                             heights[(size_t)row * (size_t)side + (size_t)left];
            const float dZ = heights[(size_t)down * (size_t)side + (size_t)col] -
                             heights[(size_t)up * (size_t)side + (size_t)col];
            const float sx = (float)(right - left) * stepX;
            const float sz = (float)(down - up) * stepZ;
            Vec3 normal = Vec3{-dX, sx * sz, -dZ};
            const float length = normal.length();
            if (length > 1e-5f) normal = normal / length;

            const size_t at = ((size_t)row * (size_t)side + (size_t)col) * kMeshVertexFloats;
            part.vertices[at + kMeshPositionOffset + 0] = x;
            part.vertices[at + kMeshPositionOffset + 1] = y;
            part.vertices[at + kMeshPositionOffset + 2] = z;
            part.vertices[at + kMeshNormalOffset + 0] = normal.x;
            part.vertices[at + kMeshNormalOffset + 1] = normal.y;
            part.vertices[at + kMeshNormalOffset + 2] = normal.z;
            part.vertices[at + kMeshTexCoordOffset + 0] = (float)col / (float)cells;
            part.vertices[at + kMeshTexCoordOffset + 1] = (float)row / (float)cells;
        }
    });

    // The indices: two triangles per square.
    for (int row = 0; row < cells; ++row) {
        for (int col = 0; col < cells; ++col) {
            const unsigned int a = (unsigned int)(row * side + col);
            const unsigned int b = a + 1u;
            const unsigned int c = a + (unsigned int)side;
            const unsigned int d = c + 1u;
            part.indices.push_back(a);
            part.indices.push_back(c);
            part.indices.push_back(b);
            part.indices.push_back(b);
            part.indices.push_back(c);
            part.indices.push_back(d);
        }
    }

    // The bounds, the same ones an imported model carries: the editor draws the
    // box around the body, and the physics uses the half sizes.
    asset.boundsMin = Vec3{-halfWidth, settings.minHeight, -halfDepth};
    asset.boundsMax = Vec3{halfWidth, settings.maxHeight, halfDepth};
    asset.center = (asset.boundsMin + asset.boundsMax) * 0.5f;
    asset.half = (asset.boundsMax - asset.boundsMin) * 0.5f;
    asset.radius = asset.half.length();
    asset.scale = 1.0f;
    asset.parts.push_back(std::move(part));
    return asset;
}

} // namespace s3d
