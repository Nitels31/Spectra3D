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

// How much of the ground around a square has to stand above it before the
// square is called a valley. The heights are in 0..1 and the ground around a
// valley is a little higher than the valley itself, so the number is a
// multiple of that difference: four means "a fifth of the range of the ground
// above me", which on a twelve-metre hill is two and a half metres.
constexpr float kShadeStrength = 4.0f;

// How big the little bumps are, and how fine they are.
//
// The bumps are in the ground's own range of 0..1 and are a hundredth of it at
// the most: on a twelve-metre hill that is a hand's width of ground under
// your boot, which is what a hill looks like from ten metres away.
constexpr float kDetailAmplitude = 0.01f;
constexpr float kDetailFrequency = 14.0f;

// How wet a place is, 0..1: the ground is drier in some parts of it than in
// others, and the drier parts are a different colour.
constexpr float kMoistureFrequency = 0.45f;

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

// The colour of the ground at a height, a steepness and a wetness.
//
// The bands are mixed rather than cut: sand at the bottom, the ground on the
// slopes, stone on the ground that is too steep to hold anything, and the top
// of the ground last of all. A line drawn across the hill where one colour
// stops and the next begins is a line you can see from the whole window, so
// the bands fade into each other instead.
//
// The wetness is what makes the ground a ground and not a colour scheme: the
// same biome is drier in one place and wetter in another, and the two are
// mixed in here rather than being two separate biomes.
void biomeColor(const LandformPalette& palette, float height, float slope, float wet, float out[3]) {
    // The ground itself: the dry version of it and the wet one, mixed by how
    // wet the place is.
    float ground[3];
    mixColor(palette.dry, palette.mid, wet, ground);

    // And the bottom of the ground: sand, soil, ice, ash - whatever the biome
    // has at the bottom, and the ground comes out of it.
    float base[3];
    mixColor(palette.low, ground, smoothstep(0.02f, 0.16f, height), base);

    // Stone takes the ground that is too steep for anything to grow on. The
    // slope is measured from the normal, so it is the same on a hill and on a
    // wall.
    float stony[3];
    mixColor(base, palette.rock, smoothstep(0.30f, 0.62f, slope), stony);

    // The top of the ground is the last thing on it, and it does not care how
    // steep it is - except that a slope too steep to hold it is bare.
    const float hold = 1.0f - smoothstep(0.55f, 0.85f, slope);
    mixColor(stony, palette.high, smoothstep(0.70f, 0.88f, height) * hold, out);
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
//
// All eight neighbours are asked, and the one that is the steepest way down
// is the one the material goes to. The diagonals count for less, because they
// are further away: the same difference of height over a longer step is a
// gentler slope, and a square that slides diagonally when it should not has
// ground with edges in it.
void thermalErode(std::vector<float>& heights, int side, int passes) {
    if (passes <= 0 || side < 3) return;
    std::vector<float> moved(heights.size(), 0.0f);

    // The eight neighbours, and how far each of them is: the step to the side
    // is one square and the step to the corner is one and a half.
    static const int kNeighbours[8][3] = {
        {-1, 0, 2}, {1, 0, 2}, {0, -1, 2}, {0, 1, 2},
        {-1, -1, 3}, {1, -1, 3}, {-1, 1, 3}, {1, 1, 3},
    };

    for (int pass = 0; pass < passes; ++pass) {
        std::fill(moved.begin(), moved.end(), 0.0f);
        for (int row = 0; row < side; ++row) {
            for (int col = 0; col < side; ++col) {
                const size_t at = (size_t)row * (size_t)side + (size_t)col;
                const float here = heights[at];

                // The steepest way down. The water goes there.
                size_t lowestAt = at;
                float lowest = here;
                float lowestSlope = 0.0f;
                for (const auto& n : kNeighbours) {
                    const int r = row + n[0];
                    const int c = col + n[1];
                    if (r < 0 || r >= side || c < 0 || c >= side) continue;
                    const size_t other = (size_t)r * (size_t)side + (size_t)c;
                    // The slope of the way down to this neighbour, over the
                    // distance to it: the height difference of two is the
                    // same on the diagonal and on the side, and the diagonal
                    // is further away, so it is the gentler of the two.
                    const float slope = (here - heights[other]) * (float)n[2];
                    if (slope > lowestSlope) {
                        lowestSlope = slope;
                        lowest = heights[other];
                        lowestAt = other;
                    }
                }

                // The height the neighbour has to be below for the ground to
                // stand: the talus, over the distance to it.
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

const char* landformBiomeName(LandformBiome biome) {
    switch (biome) {
    case LandformBiome::Grassland: return "landform.biome.grassland";
    case LandformBiome::Desert: return "landform.biome.desert";
    case LandformBiome::Snow: return "landform.biome.snow";
    case LandformBiome::Volcanic: return "landform.biome.volcanic";
    case LandformBiome::Jungle: return "landform.biome.jungle";
    case LandformBiome::Tundra: return "landform.biome.tundra";
    case LandformBiome::Savanna: return "landform.biome.savanna";
    case LandformBiome::Alpine: return "landform.biome.alpine";
    }
    return "landform.biome.grassland";
}

const char* landformBrushName(LandformBrush brush) {
    switch (brush) {
    case LandformBrush::Raise: return "landform.brush.raise";
    case LandformBrush::Lower: return "landform.brush.lower";
    case LandformBrush::Smooth: return "landform.brush.smooth";
    case LandformBrush::Flatten: return "landform.brush.flatten";
    }
    return "landform.brush.raise";
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
    normalizeHeights();

    // How much of the ground around a square stands above it. It is measured
    // here, before the little bumps are put on, so that a bump does not make
    // every square of the ground look like the bottom of a hole.
    measureShade();

    // The little bumps: the detail that makes the ground ground and not a
    // shape. They are put on after the sliding and the smoothing, because
    // both of them would take them off again.
    addDetail(halfWidth, halfDepth, stepX, stepZ);

    // And the range once more: the bumps may have pushed the highest and the
    // lowest point of the ground out a little, and the ground is still the
    // ground that was asked for.
    normalizeHeights();

    lowest_ = settings_.minHeight;
    highest_ = settings_.maxHeight;
}

// The heights so far, from 0 to 1.
//
// The ground is stretched so that its lowest point is 0 and its highest is 1:
// the heights that were asked for are given to it afterwards, and a ground
// that only uses half of the range it was given is a ground nobody asked for.
void Landform::normalizeHeights() {
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
}

// How much of the ground around a square stands above it, 0..1.
//
// A square in a valley is lower than the ground around it, and a square on a
// ridge is higher: the difference is what a valley is. It is kept per square
// because it is what the colour of the ground is made of - the valleys are
// darker, because the sky does not reach them.
void Landform::measureShade() {
    shade_.assign(heights_.size(), 0.0f);
    if (side_ < 3) return;

    for (int row = 0; row < side_; ++row) {
        for (int col = 0; col < side_; ++col) {
            const size_t at = (size_t)row * (size_t)side_ + (size_t)col;
            float sum = 0.0f;
            int count = 0;
            for (int dr = -1; dr <= 1; ++dr) {
                for (int dc = -1; dc <= 1; ++dc) {
                    if (dr == 0 && dc == 0) continue;
                    const int r = row + dr;
                    const int c = col + dc;
                    if (r < 0 || r >= side_ || c < 0 || c >= side_) continue;
                    sum += heights_[(size_t)r * (size_t)side_ + (size_t)c];
                    ++count;
                }
            }
            if (count == 0) continue;
            const float around = sum / (float)count;
            shade_[at] = clamp01((around - heights_[at]) * kShadeStrength);
        }
    }
}

// The little bumps on the surface of the ground.
//
// A noise of its own, much finer than the ground it is put on: a hill is
// metres across and the bumps on it are centimetres. The bumps are centred
// on zero, so the ground does not grow a hill on top of every other hill.
void Landform::addDetail(float halfWidth, float halfDepth, float stepX, float stepZ) {
    if (settings_.detail <= 0.0f) return;

    const float frequency = settings_.frequency * kDetailFrequency;
    const float amplitude = std::min(1.0f, settings_.detail) * kDetailAmplitude;

    for (int row = 0; row < side_; ++row) {
        const float z = -halfDepth + (float)row * stepZ;
        for (int col = 0; col < side_; ++col) {
            const float x = -halfWidth + (float)col * stepX;
            const float fine = fbm(x * frequency, z * frequency, settings_.seed + 7919u, 3);
            heights_[(size_t)row * (size_t)side_ + (size_t)col] +=
                (fine - 0.5f) * 2.0f * amplitude;
        }
    }
}

// --- the brush ---------------------------------------------------------------

// Starts a stroke: the height the flatten brush levels the ground to.
void Landform::beginStroke(float x, float z) {
    strokeHeight_ = ready() ? heightAt(x, z) : 0.0f;
}

// Paints the ground.
//
// The brush is a circle on the ground with a soft edge: the middle of it does
// the most and the rim does the least, so a stroke has no rim to see.
bool Landform::paint(float x, float z, float radius, float amount, LandformBrush brush) {
    if (!ready() || radius <= 0.0f) return false;

    const float span = settings_.maxHeight - settings_.minHeight;
    if (span <= 1e-4f) return false;

    const float halfWidth = settings_.width * 0.5f;
    const float halfDepth = settings_.depth * 0.5f;
    const float stepX = settings_.width / (float)settings_.cells;
    const float stepZ = settings_.depth / (float)settings_.cells;

    // The squares under the brush. The brush is a circle and the squares are
    // in a grid, so the brush covers the squares whose middle is inside it.
    const int colFrom = std::max(0, (int)std::floor((x - radius + halfWidth) / stepX));
    const int colTo = std::min(side_ - 1, (int)std::ceil((x + radius + halfWidth) / stepX));
    const int rowFrom = std::max(0, (int)std::floor((z - radius + halfDepth) / stepZ));
    const int rowTo = std::min(side_ - 1, (int)std::ceil((z + radius + halfDepth) / stepZ));

    // The amount in the ground's own units: the heights are 0..1 and the
    // world is `span` high.
    const float units = amount / span;

    // The smoothing brush looks at the neighbours, and the neighbours must be
    // the ones of the ground before the brush touched it - or the ground would
    // be smoothed towards itself, stroke after stroke.
    const bool smooth = brush == LandformBrush::Smooth;
    if (smooth) scratch_ = heights_;

    bool changed = false;
    for (int row = rowFrom; row <= rowTo; ++row) {
        const float pz = -halfDepth + (float)row * stepZ;
        for (int col = colFrom; col <= colTo; ++col) {
            const float px = -halfWidth + (float)col * stepX;
            const float dx = px - x;
            const float dz = pz - z;
            const float distance = std::sqrt(dx * dx + dz * dz);
            if (distance > radius) continue;

            // How much of the stroke this square gets: all of it in the
            // middle, none of it at the rim.
            const float t = distance / radius;
            const float falloff = 1.0f - t * t * (3.0f - 2.0f * t);

            const size_t at = (size_t)row * (size_t)side_ + (size_t)col;
            const float before = heights_[at];
            switch (brush) {
            case LandformBrush::Raise:
                heights_[at] += units * falloff;
                break;
            case LandformBrush::Lower:
                heights_[at] -= units * falloff;
                break;
            case LandformBrush::Smooth: {
                // The average of the eight around it: the bumps are taken off
                // and the shape of the hill is left.
                float sum = 0.0f;
                int count = 0;
                for (int dr = -1; dr <= 1; ++dr) {
                    for (int dc = -1; dc <= 1; ++dc) {
                        if (dr == 0 && dc == 0) continue;
                        const int r = row + dr;
                        const int c = col + dc;
                        if (r < 0 || r >= side_ || c < 0 || c >= side_) continue;
                        sum += scratch_[(size_t)r * (size_t)side_ + (size_t)c];
                        ++count;
                    }
                }
                if (count == 0) break;
                const float average = sum / (float)count;
                heights_[at] += (average - heights_[at]) * falloff;
                break;
            }
            case LandformBrush::Flatten:
                // Level with the height the stroke began at.
                heights_[at] += (strokeHeight_ - heights_[at]) * falloff;
                break;
            }

            // The ground may go above the top it was given and below the
            // bottom of it - a hill that was raised is a hill that is higher
            // than the ground asked for - but not so far that it is a
            // mistake.
            heights_[at] = std::clamp(heights_[at], -1.0f, 2.0f);
            if (heights_[at] != before) changed = true;
        }
    }

    if (changed) {
        painted_ = true;
        // The valleys of the ground have moved, so the shade of them is
        // measured again: the ground that is darker in the valley is the
        // ground that has a valley in it.
        measureShade();
    }
    return changed;
}

// Where a ray meets the ground.
//
// The ground is a heightfield, so there is no triangle to hit: the ray is
// walked along, and where it goes under the ground is where it hits. The step
// is half a square, so the ground cannot be stepped over, and the crossing
// between two steps is found more precisely by halving it.
bool Landform::rayHit(const Vec3& origin, const Vec3& direction, float& outDistance) const {
    if (!ready()) return false;

    const float halfWidth = settings_.width * 0.5f;
    const float halfDepth = settings_.depth * 0.5f;
    const float step = std::fmax(0.05f, std::fmin(settings_.width, settings_.depth) /
                                            (float)settings_.cells * 0.5f);

    float previousT = 0.0f;
    float previousGap = origin.y - heightAt(origin.x, origin.z);
    // The ray is walked as far as the ground is wide and then some: a ground
    // that is not there is a ground the ray does not hit.
    const float limit = settings_.width * 2.0f + settings_.depth * 2.0f + 100.0f;

    for (float t = step; t < limit; t += step) {
        const Vec3 point = origin + direction * t;
        const float gap = point.y - heightAt(point.x, point.z);
        if (gap <= 0.0f && previousGap > 0.0f) {
            // Between the two steps: the crossing, found by halving.
            float low = previousT;
            float high = t;
            for (int i = 0; i < 16; ++i) {
                const float middle = (low + high) * 0.5f;
                const Vec3 between = origin + direction * middle;
                if (between.y - heightAt(between.x, between.z) > 0.0f) low = middle;
                else high = middle;
            }
            const Vec3 hit = origin + direction * high;
            // Only the ground itself is the ground: outside it the height is
            // the height of the edge, and the edge is not a hill to paint.
            if (std::fabs(hit.x) > halfWidth || std::fabs(hit.z) > halfDepth) return false;
            outDistance = high;
            return true;
        }
        previousT = t;
        previousGap = gap;
    }
    return false;
}

// Puts a ground that was painted back into the landform.
bool Landform::setHeights(const std::vector<float>& heights) {
    if (heights.size() != heights_.size() || heights_.empty()) return false;
    heights_ = heights;
    painted_ = true;
    // The valleys of the ground are the valleys of the heights that were
    // given, so the shade of them is measured again.
    measureShade();
    return true;
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

    // The height of the ground at a point, in the ground's own frame: the
    // middle of the ground is 0, and the ground is half as high above it as
    // it is below. It is the same height the mesh has, so a body put on the
    // ground stands on the ground that is drawn.
    return (n - 0.5f) * (settings_.maxHeight - settings_.minHeight);
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
            // The height is measured from the middle of the ground, because
            // the body it belongs to stands there: the mesh is drawn around its
            // own middle, and the ground is the height it was asked to be.
            const float y = (heights_[at] - 0.5f) * span;

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
                // How wet this part of the ground is: the ground is drier in
                // some places than in others, and the two are not the same
                // colour.
                const float wet = fbm(x * settings_.frequency * kMoistureFrequency,
                                      z * settings_.frequency * kMoistureFrequency,
                                      settings_.seed + 4242u, 3);
                float color[3];
                biomeColor(landformPalette(settings_.biome), heights_[at], slope, wet, color);

                // The valleys are darker, because the sky does not reach them.
                // This is what makes a valley look like a valley and not like a
                // stripe of a different colour.
                const float shade =
                    1.0f - std::min(1.0f, settings_.shading) * 0.45f *
                              (at < shade_.size() ? shade_[at] : 0.0f);
                part.colors[at * 3 + 0] = color[0] * shade;
                part.colors[at * 3 + 1] = color[1] * shade;
                part.colors[at * 3 + 2] = color[2] * shade;
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
    //
    // They are measured from the middle of the ground, which is where the
    // body stands: the mesh is drawn around its own middle, and the ground is
    // the height it was asked to be.
    asset.boundsMin = Vec3{-halfWidth, -span * 0.5f, -halfDepth};
    asset.boundsMax = Vec3{halfWidth, span * 0.5f, halfDepth};
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
