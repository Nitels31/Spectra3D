#include "scene/Landform.hpp"

#include "core/Math.hpp"
#include "scene/Noise.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <random>
#include <thread>
#include <vector>

namespace s3d {
namespace {

// The number of squares a landform may be cut into. A hundred thousand squares
// is a mesh nobody can look at and a machine nobody can afford.
constexpr int kMinCells = 2;
constexpr int kMaxCells = 256;

// How many times the picture repeats over the ground. The ground is tens of
// metres across and the picture is a square of grass, so one copy of it would
// be stretched over the whole thing.
constexpr float kTextureRepeat = 12.0f;

// How far a square of ground may stand above its neighbour before it slides
// down, and how much of the difference it gives up when it does. Both are in
// the ground's own range of 0..1, so they mean the same thing whatever height
// the ground is finally given.
constexpr float kTalus = 0.035f;
constexpr float kErosionRate = 0.5f;

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

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// 0 below `from`, 1 above `to`, and a smooth curve in between.
float smoothstep(float from, float to, float v) {
    if (to <= from) return v >= to ? 1.0f : 0.0f;
    const float t = clamp01((v - from) / (to - from));
    return t * t * (3.0f - 2.0f * t);
}

void mixColor(const float a[3], const float b[3], float t, float out[3]) {
    out[0] = a[0] + (b[0] - a[0]) * t;
    out[1] = a[1] + (b[1] - a[1]) * t;
    out[2] = a[2] + (b[2] - a[2]) * t;
}

// The colour of the ground at a height and a steepness.
//
// Four bands, and they are mixed rather than cut: sand at the bottom, grass
// on the slopes, stone on the ground that is too steep to hold anything, and
// snow at the top. A line drawn across the hill where one colour stops and
// the next begins is a line you can see from the whole window, so the bands
// fade into each other instead.
void biomeColor(float height, float slope, float out[3]) {
    const float sand[3] = {0.80f, 0.74f, 0.55f};
    const float grass[3] = {0.38f, 0.58f, 0.26f};
    const float rock[3] = {0.45f, 0.43f, 0.41f};
    const float snow[3] = {0.93f, 0.95f, 0.98f};

    float base[3];
    mixColor(sand, grass, smoothstep(0.02f, 0.18f, height), base);

    // Stone takes the ground that is too steep for anything to grow on. The
    // slope is measured from the normal, so it is the same on a hill and on a
    // wall.
    float stony[3];
    mixColor(base, rock, smoothstep(0.30f, 0.62f, slope), stony);

    // Snow is on the top, and it does not care how steep it is - except that
    // a slope too steep to hold it is bare.
    mixColor(stony, snow, smoothstep(0.70f, 0.88f, height), out);
}

// The height of the ground at a point, before the ground is eroded: 0..1.
//
// The shape is what is done to the noise, and each of them is a different
// answer to the same question: what does a piece of ground look like.
float rawHeight(const LandformSettings& settings, float x, float z) {
    // The frequency of the noise, with the variation of this ground added to
    // it. The variation is a random number, and it is added to the frequency
    // so that the features of the ground come out a little bigger or a little
    // smaller than they were the last time: two grounds made one after the
    // other are not the same ground twice.
    const float frequency = settings.frequency + std::fmax(0.0f, settings.variation);
    const float fx = x * frequency;
    const float fz = z * frequency;

    // The two noises the ground is made of: round shapes and sharp ones. The
    // ridge setting says how much of the sharp one there is.
    const float soft = warpedFbm(fx, fz, settings.seed, settings.octaves, settings.warp);
    const float sharp = ridgedFbm(fx, fz, settings.seed, settings.octaves, settings.lacunarity,
                                  settings.gain);
    const float ridge = clamp01(settings.ridge);
    float n = soft * (1.0f - ridge) + sharp * ridge;

    // The ground as the noise makes it, before the shape is put on it. The
    // shapes that have ground on them need it: a plateau with nothing on it
    // is a table.
    const float plain = n;

    const float halfWidth = settings.width * 0.5f;
    const float halfDepth = settings.depth * 0.5f;
    // How far the point is from the middle of the ground, 0 at the middle and
    // 1 at the corner.
    const float nx = halfWidth > 0.0f ? x / halfWidth : 0.0f;
    const float nz = halfDepth > 0.0f ? z / halfDepth : 0.0f;
    const float distance = std::sqrt(nx * nx + nz * nz);

    switch (settings.shape) {
    case LandformShape::Hills:
        // The middle of the range is where the ground spends most of its
        // time, so the curve is bent: the valleys become flat and the tops
        // become round.
        n = n * n * (3.0f - 2.0f * n);
        break;

    case LandformShape::Mountains:
        // The peaks are high and the ground between them is low, and the
        // whole thing is measured against a very large shape so that the
        // range has a lowland in it and not one height everywhere.
        n = std::pow(clamp01(n), 1.35f);
        {
            const float mask = fbm(fx * 0.22f, fz * 0.22f, settings.seed + 31337u, 3);
            // How much of the range is lowland: the rest of it is mountain.
            const float lowland = clamp01(settings.lowland);
            n *= lowland + (1.0f - lowland) * mask;
        }
        break;

    case LandformShape::Island:
        // The ground falls away into the water at the edges. How far from the
        // middle it starts to fall is asked for.
        n *= 1.0f - smoothstep(clamp01(settings.shore), 1.0f, distance);
        break;

    case LandformShape::Plateau: {
        // Flat tops with steps between them: the height is cut into levels,
        // and the step from one level to the next is rounded just enough to
        // walk on. How many levels there are and how wide the step is are
        // asked for.
        const float levels = std::fmax(1.0f, (float)settings.levels);
        const float scaled = clamp01(n) * levels;
        const float level = std::floor(scaled);
        const float frac = scaled - level;
        const float width = std::clamp(settings.roundness, 0.02f, 1.0f);
        const float step = smoothstep(0.5f - width * 0.5f, 0.5f + width * 0.5f, frac);
        const float terraced = (level + step) / levels;

        // And the ordinary ground is put on top of it: the same noise, bent
        // the way the hills bend it, so the flat tops are ground with steps
        // in it and not a table. How much of it is put on is asked for, and
        // none of it leaves the tops as flat as they were.
        const float rolling = plain * plain * (3.0f - 2.0f * plain);
        const float ground = clamp01(settings.ground);
        n = terraced + (rolling - terraced) * ground;
        break;
    }

    case LandformShape::Canyon: {
        // A river cut through the ground. The path meanders, because a river
        // that runs straight is a ditch.
        const float meander = fbm(fx * 0.30f, 7.3f, settings.seed + 977u, 2) - 0.5f;
        const float pathZ = meander * settings.depth * std::fmax(0.0f, settings.meander);
        const float across = std::fabs(z - pathZ);
        const float width = std::fmax(0.5f, settings.width * clamp01(settings.river));
        const float carve = 1.0f - smoothstep(0.0f, width, across);
        // The ground is cut down to the bed of the river, and the sides of the
        // cut are steeper than the ground around it. How deep the cut is is
        // asked for.
        const float cut = clamp01(settings.cut);
        n = n * (1.0f - cut * carve) - cut * 0.125f * carve;
        n = clamp01(n);
        break;
    }

    case LandformShape::Dunes:
        // Round hills, stretched along one axis, and the sand between them is
        // flat. How far they are stretched is asked for.
        n = billowFbm(fx * 0.30f, fz * std::fmax(0.05f, settings.stretch), settings.seed,
                      settings.octaves, settings.lacunarity, settings.gain);
        break;
    }

    return clamp01(n);
}

// Material that stands above the angle of repose slides downhill.
//
// This is the step that turns a field of noise into ground: the spikes are
// taken off the top, the hollows fill up, and what is left has ridges and
// valleys instead of a field of cubes. The moves are gathered in a buffer and
// applied together, so the order the squares are visited in cannot change
// the answer.
void thermalErode(std::vector<float>& heights, int side, int passes) {
    if (passes <= 0 || side < 3) return;
    std::vector<float> moved(heights.size(), 0.0f);

    for (int pass = 0; pass < passes; ++pass) {
        std::fill(moved.begin(), moved.end(), 0.0f);
        for (int row = 0; row < side; ++row) {
            for (int col = 0; col < side; ++col) {
                const size_t at = (size_t)row * (size_t)side + (size_t)col;
                const float here = heights[at];

                // The lowest of the four neighbours. The water goes there.
                size_t lowestAt = at;
                float lowest = here;
                const int neighbours[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                for (const auto& n : neighbours) {
                    const int r = row + n[0];
                    const int c = col + n[1];
                    if (r < 0 || r >= side || c < 0 || c >= side) continue;
                    const size_t other = (size_t)r * (size_t)side + (size_t)c;
                    if (heights[other] < lowest) {
                        lowest = heights[other];
                        lowestAt = other;
                    }
                }

                const float difference = here - lowest;
                if (difference <= kTalus) continue;
                // A quarter of the difference, and no more than the square can
                // give: a square that gives away more than it has would sink
                // below its neighbour and the ground would start to shake.
                const float amount = std::min((difference - kTalus) * kErosionRate * 0.25f,
                                              (here - lowest) * 0.25f);
                moved[at] -= amount;
                moved[lowestAt] += amount;
            }
        }
        for (size_t i = 0; i < heights.size(); ++i) heights[i] += moved[i];
    }
}

// The blur of the ground: every square is pulled a little towards the average
// of the eight around it.
void smoothHeights(std::vector<float>& heights, int side, float amount, int passes) {
    if (amount <= 0.0f || passes <= 0 || side < 3) return;
    std::vector<float> next(heights.size(), 0.0f);

    for (int pass = 0; pass < passes; ++pass) {
        for (int row = 0; row < side; ++row) {
            for (int col = 0; col < side; ++col) {
                float sum = 0.0f;
                float weight = 0.0f;
                for (int dr = -1; dr <= 1; ++dr) {
                    for (int dc = -1; dc <= 1; ++dc) {
                        const int r = row + dr;
                        const int c = col + dc;
                        if (r < 0 || r >= side || c < 0 || c >= side) continue;
                        // The middle of the three by three counts most, the
                        // sides less, the corners least: a plain average
                        // would flatten the ground as much as the blur is
                        // worth.
                        const float w = (dr == 0 && dc == 0) ? 4.0f
                                                             : (dr == 0 || dc == 0 ? 2.0f : 1.0f);
                        sum += heights[(size_t)r * (size_t)side + (size_t)c] * w;
                        weight += w;
                    }
                }
                const size_t at = (size_t)row * (size_t)side + (size_t)col;
                const float blurred = sum / weight;
                next[at] = heights[at] + (blurred - heights[at]) * amount;
            }
        }
        heights.swap(next);
    }
}

} // namespace

const char* landformShapeName(LandformShape shape) {
    switch (shape) {
    case LandformShape::Hills: return "landform.shape.hills";
    case LandformShape::Mountains: return "landform.shape.mountains";
    case LandformShape::Island: return "landform.shape.island";
    case LandformShape::Plateau: return "landform.shape.plateau";
    case LandformShape::Canyon: return "landform.shape.canyon";
    case LandformShape::Dunes: return "landform.shape.dunes";
    }
    return "landform.shape.hills";
}

float landformVariation() {
    // The number is drawn from the machine's own random source, and the
    // engine that draws it is seeded once: two grounds made in a row are not
    // the same ground twice, and a ground made with the number that is drawn
    // is the same ground wherever it is made.
    static std::mt19937 engine{std::random_device{}()};
    static std::uniform_real_distribution<float> pick(0.0005f, 0.02f);
    return pick(engine);
}

void Landform::build(const LandformSettings& settings) {
    settings_ = settings;
    settings_.cells = std::clamp(settings.cells, kMinCells, kMaxCells);
    settings_.octaves = std::clamp(settings.octaves, 1, 12);
    settings_.erosion = std::clamp(settings.erosion, 0, 24);
    if (settings_.width < 0.1f) settings_.width = 0.1f;
    if (settings_.depth < 0.1f) settings_.depth = 0.1f;
    // A ground whose highest point is below its lowest is a ground nobody
    // asked for: the two are put the right way round.
    if (settings_.maxHeight < settings_.minHeight)
        std::swap(settings_.maxHeight, settings_.minHeight);

    side_ = settings_.cells + 1;
    heights_.assign((size_t)side_ * (size_t)side_, 0.0f);

    const float halfWidth = settings_.width * 0.5f;
    const float halfDepth = settings_.depth * 0.5f;
    const float stepX = settings_.width / (float)settings_.cells;
    const float stepZ = settings_.depth / (float)settings_.cells;

    // The heights first, one per vertex, in rows. A row's squares do not
    // depend on the next row, so the rows are made at the same time.
    forEachRow(side_, settings_.threads, [&](int row) {
        const float z = -halfDepth + (float)row * stepZ;
        for (int col = 0; col < side_; ++col) {
            const float x = -halfWidth + (float)col * stepX;
            heights_[(size_t)row * (size_t)side_ + (size_t)col] =
                rawHeight(settings_, x, z);
        }
    });

    // Then the ground is let slide, and then it is smoothed: the sliding
    // takes the spikes off, the smoothing takes the noise out of what is
    // left.
    thermalErode(heights_, side_, settings_.erosion);
    smoothHeights(heights_, side_, std::min(1.0f, settings_.smoothing) * 0.5f, 2);

    // The range of the ground is the range that was asked for. The noise
    // reaches from 0 to 1 and not always both ends, and a ground that is
    // always half as high as it was asked to be is a ground nobody asked for.
    float lowest = 1.0f;
    float highest = 0.0f;
    for (float h : heights_) {
        if (h < lowest) lowest = h;
        if (h > highest) highest = h;
    }
    const float range = highest - lowest;
    for (float& h : heights_) {
        h = range > 1e-5f ? (h - lowest) / range : 0.0f;
    }

    lowest_ = settings_.minHeight;
    highest_ = settings_.maxHeight;
}

float Landform::heightAt(float x, float z) const {
    if (!ready()) return 0.0f;

    const float halfWidth = settings_.width * 0.5f;
    const float halfDepth = settings_.depth * 0.5f;
    const float stepX = settings_.width / (float)settings_.cells;
    const float stepZ = settings_.depth / (float)settings_.cells;

    // Where the point is in the grid, in squares.
    const float u = (x + halfWidth) / stepX;
    const float v = (z + halfDepth) / stepZ;
    // Outside the ground is the ground's own edge: the lowest square of the
    // row or the column the point is beside, and not the lowest square of
    // the whole ground.
    const float clampedU = std::fmin(std::fmax(u, 0.0f), (float)(side_ - 1));
    const float clampedV = std::fmin(std::fmax(v, 0.0f), (float)(side_ - 1));

    const int col = (int)clampedU;
    const int row = (int)clampedV;
    const int colNext = std::min(col + 1, side_ - 1);
    const int rowNext = std::min(row + 1, side_ - 1);
    const float fu = clampedU - (float)col;
    const float fv = clampedV - (float)row;

    const float h00 = heights_[(size_t)row * (size_t)side_ + (size_t)col];
    const float h10 = heights_[(size_t)row * (size_t)side_ + (size_t)colNext];
    const float h01 = heights_[(size_t)rowNext * (size_t)side_ + (size_t)col];
    const float h11 = heights_[(size_t)rowNext * (size_t)side_ + (size_t)colNext];

    const float top = h00 + (h10 - h00) * fu;
    const float bottom = h01 + (h11 - h01) * fu;
    const float n = top + (bottom - top) * fv;

    return settings_.minHeight + n * (settings_.maxHeight - settings_.minHeight);
}

MeshAsset Landform::toMesh() const {
    MeshAsset asset;
    asset.name = "landform";
    if (!ready()) return asset;

    const int cells = settings_.cells;
    const int side = side_;
    const float halfWidth = settings_.width * 0.5f;
    const float halfDepth = settings_.depth * 0.5f;
    const float stepX = settings_.width / (float)cells;
    const float stepZ = settings_.depth / (float)cells;
    const float span = settings_.maxHeight - settings_.minHeight;

    MeshPart part;
    // Every row writes its own stretch of the vertices, so the rows can be made
    // at the same time and still come out in the order the indices expect.
    part.vertices.assign((size_t)side * (size_t)side * kMeshVertexFloats, 0.0f);
    part.indices.reserve((size_t)cells * (size_t)cells * 6);
    // The colour of every vertex, when the ground is coloured by its height
    // and its steepness. A part without them is drawn in its flat colour, the
    // way every imported model is.
    if (settings_.biomes) part.colors.assign((size_t)side * (size_t)side * 3, 1.0f);

    for (int row = 0; row < side; ++row) {
        for (int col = 0; col < side; ++col) {
            const size_t at = (size_t)row * (size_t)side + (size_t)col;
            const float x = -halfWidth + (float)col * stepX;
            const float z = -halfDepth + (float)row * stepZ;
            const float y = settings_.minHeight + heights_[at] * span;

            // The normal comes from the slope: the height a step to the left
            // and a step to the front.
            const int left = std::max(0, col - 1);
            const int right = std::min(side - 1, col + 1);
            const int up = std::max(0, row - 1);
            const int down = std::min(side - 1, row + 1);
            const float dX = (heights_[(size_t)row * (size_t)side + (size_t)right] -
                              heights_[(size_t)row * (size_t)side + (size_t)left]) *
                             span;
            const float dZ = (heights_[(size_t)down * (size_t)side + (size_t)col] -
                              heights_[(size_t)up * (size_t)side + (size_t)col]) *
                             span;
            const float sx = (float)(right - left) * stepX;
            const float sz = (float)(down - up) * stepZ;
            Vec3 normal = Vec3{-dX, sx * sz, -dZ};
            const float length = normal.length();
            if (length > 1e-5f) normal = normal / length;

            const size_t vertex = at * kMeshVertexFloats;
            part.vertices[vertex + kMeshPositionOffset + 0] = x;
            part.vertices[vertex + kMeshPositionOffset + 1] = y;
            part.vertices[vertex + kMeshPositionOffset + 2] = z;
            part.vertices[vertex + kMeshNormalOffset + 0] = normal.x;
            part.vertices[vertex + kMeshNormalOffset + 1] = normal.y;
            part.vertices[vertex + kMeshNormalOffset + 2] = normal.z;
            part.vertices[vertex + kMeshTexCoordOffset + 0] = (float)col / (float)cells;
            part.vertices[vertex + kMeshTexCoordOffset + 1] = (float)row / (float)cells;

            if (settings_.biomes) {
                // How steep the ground is here: 0 is flat, 1 is a wall.
                const float slope = 1.0f - normal.y;
                float color[3];
                biomeColor(heights_[at], slope, color);
                part.colors[at * 3 + 0] = color[0];
                part.colors[at * 3 + 1] = color[1];
                part.colors[at * 3 + 2] = color[2];
            }
        }
    }

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
    asset.boundsMin = Vec3{-halfWidth, settings_.minHeight, -halfDepth};
    asset.boundsMax = Vec3{halfWidth, settings_.maxHeight, halfDepth};
    asset.center = (asset.boundsMin + asset.boundsMax) * 0.5f;
    asset.half = (asset.boundsMax - asset.boundsMin) * 0.5f;
    asset.radius = asset.half.length();
    asset.scale = 1.0f;
    // The colour of the ground, and the picture on it. The picture tiles: the
    // UVs run on past the edge of the ground, and the texture repeats there.
    part.color[0] = settings_.color[0];
    part.color[1] = settings_.color[1];
    part.color[2] = settings_.color[2];
    for (size_t i = 0; i < part.vertices.size(); i += kMeshVertexFloats) {
        part.vertices[i + kMeshTexCoordOffset + 0] *= kTextureRepeat;
        part.vertices[i + kMeshTexCoordOffset + 1] *= kTextureRepeat;
    }

    asset.parts.push_back(std::move(part));
    return asset;
}

} // namespace s3d
