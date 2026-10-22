#include "scene/Cloud.hpp"

#include "core/Log.hpp"
#include "scene/Noise.hpp"

#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>

namespace s3d {
namespace {

// How many puffs a cloud may be made of.
//
// A puff is a little ball of geometry, and a cloud is hundreds of them. A
// thousand puffs is a cloud that takes a second to make and a megabyte to
// draw, and nobody has ever looked at a cloud and said "it needed more".
constexpr int kMaxPuffs = 512;

// How many places are looked at for every puff that is placed.
//
// A place is thrown away when the sky there is too thin for the cloud, or
// when another puff is already standing in it, and the next place is looked
// at. Four is enough for the puffs to end up in the thick parts of the noise
// without the making of a cloud turning into a search.
constexpr int kPlacesPerPuff = 4;

// A number in 0..1 out of a hash.
//
// The hash is on the number of the puff and on the seed, so the same cloud is
// the same cloud on every machine, and the puffs of one cloud never move.
float hashUnit(unsigned int a, unsigned int b, unsigned int seed) {
    unsigned int h = (unsigned int)a * 0x8DA6B343u;
    h ^= (unsigned int)b * 0xD8163841u;
    h += seed * 0x165667B1u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return (float)(h & 0xFFFFFFu) / 16777215.0f;
}

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// 0 below `from`, 1 above `to`, and a smooth curve in between.
float smoothstep(float from, float to, float v) {
    if (to <= from) return v >= to ? 1.0f : 0.0f;
    const float t = clamp01((v - from) / (to - from));
    return t * t * (3.0f - 2.0f * t);
}

// How deep the cloud is, as a part of how wide it is.
//
// A layer of cloud reaches as far as it is wide, and a streak of cirrus does
// not.
float shapeDepth(CloudShape shape) {
    switch (shape) {
    case CloudShape::Cumulus: return 0.80f;
    case CloudShape::Stratus: return 1.00f;
    case CloudShape::Cirrus: return 0.45f;
    case CloudShape::Cumulonimbus: return 0.60f;
    }
    return 0.80f;
}

// How wide the cloud is at a height, as a part of how wide it is at the
// bottom.
//
// This is the silhouette of the shape, and it is what turns a heap of puffs
// into a heap, a layer, a streak or a tower.
float shapeProfile(CloudShape shape, float t) {
    switch (shape) {
    case CloudShape::Cumulus: {
        // A heap: full width at the bottom and rounded on top.
        const float dome = std::sqrt(std::fmax(0.0f, 1.0f - t * t));
        return 0.25f + 0.75f * dome;
    }
    case CloudShape::Stratus:
        // A layer: the same width the whole way, and a little less at the top.
        return 1.0f - 0.30f * t * t;
    case CloudShape::Cirrus:
        // Streaks: they thin out towards the top.
        return 1.0f - 0.85f * t;
    case CloudShape::Cumulonimbus:
        // A tower: narrow at the bottom and wide at the top.
        return 0.45f + 0.55f * t;
    }
    return 1.0f;
}

// What a puff of this shape looks like: wider than it is tall, or taller
// than it is wide, or flat as a sheet of paper.
Vec3 shapeStretch(CloudShape shape) {
    switch (shape) {
    case CloudShape::Cumulus: return Vec3{1.0f, 1.0f, 1.0f};
    case CloudShape::Stratus: return Vec3{1.0f, 0.45f, 1.0f};
    case CloudShape::Cirrus: return Vec3{1.90f, 0.30f, 0.70f};
    case CloudShape::Cumulonimbus: return Vec3{1.0f, 1.0f, 1.0f};
    }
    return Vec3{1.0f, 1.0f, 1.0f};
}

// Whether the bottom of the cloud is flat.
//
// A cumulus has a base: the water that is cold enough to fall out of it does
// not fall out of it halfway up, and the bottom of the heap is a line. A layer
// of cloud has a base as well, and a streak has none.
bool shapeHasFlatBase(CloudShape shape) {
    return shape == CloudShape::Cumulus || shape == CloudShape::Cumulonimbus;
}

// How much of the sky is cloud at a point, 0..1.
//
// Two things are multiplied: the noise, which says where the lumps are, and
// the shape of the cloud, which says where the cloud is at all. The product
// is what the coverage is compared with, so the coverage decides how thick
// the cloud has to be before it is cloud at all.
float cloudDensity(const CloudSettings& settings, float x, float y, float z, float halfWidth,
                   float halfDepth, float height) {
    // The scale of the lumps: a part of the cloud's own size, so a cloud that
    // is twice as wide has lumps twice as wide.
    const float frequency = 1.0f / std::fmax(1.0f, settings.spread * 0.5f);
    const float noise = (settings.mode == CloudMode::Flat)
                            ? fbm(x * frequency, y * frequency, settings.seed, settings.detail)
                            : fbm3(x * frequency, y * frequency, z * frequency, settings.seed,
                                    settings.detail);

    // Where the point is in the shape of the cloud: 0 at the middle, 1 at the
    // edge of the envelope at this height.
    const float t = height > 0.0f ? clamp01(y / height) : 0.0f;
    const float allowed = std::fmax(0.001f, shapeProfile(settings.shape, t));
    const float nx = halfWidth > 0.0f ? x / halfWidth : 0.0f;
    const float nz = halfDepth > 0.0f ? z / halfDepth : 0.0f;
    const float radial = std::sqrt(nx * nx + nz * nz);

    // The envelope: full inside it, nothing outside it.
    const float inside = 1.0f - smoothstep(allowed * 0.45f, allowed, radial);
    return clamp01(noise * inside);
}

// The colour of a point of the cloud.
//
// Two things decide it. How high the point is: the sun reaches the top of a
// cloud and hardly reaches the bottom of it. And how deep inside the cloud
// the puff is: a puff that is buried in the middle of the cloud sees no sun
// at all, and the middle of a cloud is grey.
void cloudColour(const CloudSettings& settings, float y, float buried, float out[3]) {
    const float t = settings.height > 0.0f ? clamp01(y / settings.height) : 1.0f;
    // The light on the top of it. The curve is not a straight line: the light
    // comes in fast, and the bottom of a cloud stays grey a long way up.
    const float light = smoothstep(0.10f, 0.80f, t);
    const float dark = 1.0f - 0.40f * clamp01(buried);
    for (int i = 0; i < 3; ++i)
        out[i] = (settings.shade[i] + (settings.color[i] - settings.shade[i]) * light) * dark;
}

// THE GEOMETRY OF ONE THREAD
//
// What a thread writes into: the vertices, the indices and the colours. A
// thread has one of these and nobody writes in it but its owner.
struct Chunk {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    std::vector<float> colors;
};

// One puff as a ball.
//
// The resolution is how many pieces go around it: four is a ball of eight
// triangles and cannot be told from a ball of twenty-four at any distance a
// cloud is ever seen from - and a cloud is hundreds of balls, so the
// difference is the whole cost of it.
void addSphere(Chunk& chunk, const Cloud::Puff& puff, int segments, int rings,
               const CloudSettings& settings) {
    const unsigned int base = (unsigned int)(chunk.vertices.size() / kMeshVertexFloats);
    const float rx = std::fmax(0.001f, puff.radius.x);
    const float ry = std::fmax(0.001f, puff.radius.y);
    const float rz = std::fmax(0.001f, puff.radius.z);

    for (int ring = 0; ring <= rings; ++ring) {
        const float phi = 3.14159265f * (float)ring / (float)rings;
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);
        for (int s = 0; s <= segments; ++s) {
            const float theta = 6.28318531f * (float)s / (float)segments;
            const float dx = sinPhi * std::cos(theta);
            const float dy = cosPhi;
            const float dz = sinPhi * std::sin(theta);

            const float px = puff.centre.x + dx * rx;
            const float py = puff.centre.y + dy * ry;
            const float pz = puff.centre.z + dz * rz;

            // The normal of an ellipsoid is not the normal of the ball it was
            // made from: a ball that is squashed has to be lit as a squashed
            // ball, or the light runs across it the wrong way.
            Vec3 normal{dx / rx, dy / ry, dz / rz};
            const float length = normal.length();
            if (length > 1e-5f) normal = normal / length;

            float colour[3];
            cloudColour(settings, py, puff.buried, colour);

            const size_t at = chunk.vertices.size();
            chunk.vertices.resize(at + kMeshVertexFloats);
            chunk.vertices[at + kMeshPositionOffset + 0] = px;
            chunk.vertices[at + kMeshPositionOffset + 1] = py;
            chunk.vertices[at + kMeshPositionOffset + 2] = pz;
            chunk.vertices[at + kMeshNormalOffset + 0] = normal.x;
            chunk.vertices[at + kMeshNormalOffset + 1] = normal.y;
            chunk.vertices[at + kMeshNormalOffset + 2] = normal.z;
            chunk.vertices[at + kMeshTexCoordOffset + 0] = (float)s / (float)segments;
            chunk.vertices[at + kMeshTexCoordOffset + 1] = (float)ring / (float)rings;
            chunk.colors.push_back(colour[0]);
            chunk.colors.push_back(colour[1]);
            chunk.colors.push_back(colour[2]);
        }
    }

    // Two triangles for every square of the ball.
    for (int ring = 0; ring < rings; ++ring) {
        for (int s = 0; s < segments; ++s) {
            const unsigned int a = base + (unsigned int)ring * (unsigned int)(segments + 1) +
                                   (unsigned int)s;
            const unsigned int b = a + 1u;
            const unsigned int c = a + (unsigned int)(segments + 1);
            const unsigned int d = c + 1u;
            chunk.indices.push_back(a);
            chunk.indices.push_back(b);
            chunk.indices.push_back(c);
            chunk.indices.push_back(b);
            chunk.indices.push_back(d);
            chunk.indices.push_back(c);
        }
    }
}

// One puff as a disc.
//
// The flat cloud is a picture on a plane: a heap of discs that lie in it and
// are seen from the front. The discs are opaque and they are all the same
// colour where they overlap, so what the eye sees is their union - which is
// the shape a cloud has always had in a 2D game.
void addDisc(Chunk& chunk, const Cloud::Puff& puff, int segments,
             const CloudSettings& settings) {
    const unsigned int base = (unsigned int)(chunk.vertices.size() / kMeshVertexFloats);
    const float rx = std::fmax(0.001f, puff.radius.x);
    const float ry = std::fmax(0.001f, puff.radius.y);

    // The middle of the disc, and then its edge.
    for (int i = 0; i <= segments; ++i) {
        const float angle = (i == 0) ? 0.0f : 6.28318531f * (float)(i - 1) / (float)segments;
        const float px = puff.centre.x + (i == 0 ? 0.0f : std::cos(angle) * rx);
        const float py = puff.centre.y + (i == 0 ? 0.0f : std::sin(angle) * ry);
        const float pz = puff.centre.z;

        float colour[3];
        cloudColour(settings, py, puff.buried, colour);

        const size_t at = chunk.vertices.size();
        chunk.vertices.resize(at + kMeshVertexFloats);
        chunk.vertices[at + kMeshPositionOffset + 0] = px;
        chunk.vertices[at + kMeshPositionOffset + 1] = py;
        chunk.vertices[at + kMeshPositionOffset + 2] = pz;
        // The disc faces the front, and the light comes from where it comes
        // from: the whole picture of a flat cloud is lit as one surface.
        chunk.vertices[at + kMeshNormalOffset + 0] = 0.0f;
        chunk.vertices[at + kMeshNormalOffset + 1] = 0.0f;
        chunk.vertices[at + kMeshNormalOffset + 2] = 1.0f;
        chunk.vertices[at + kMeshTexCoordOffset + 0] = 0.5f;
        chunk.vertices[at + kMeshTexCoordOffset + 1] = 0.5f;
        chunk.colors.push_back(colour[0]);
        chunk.colors.push_back(colour[1]);
        chunk.colors.push_back(colour[2]);
    }

    // A fan of triangles around the middle.
    for (int i = 0; i < segments; ++i) {
        chunk.indices.push_back(base);
        chunk.indices.push_back(base + 1u + (unsigned int)i);
        chunk.indices.push_back(base + 1u + (unsigned int)((i + 1) % segments));
    }
}

// Runs `body(puff, chunk)` for every puff, on as many threads as there are
// puffs to share.
//
// The puffs are cut into pieces and each piece is made by one thread: the
// pieces are in order, so the puffs come out in the same order whatever order
// they were made in. A cloud made with one thread is the same cloud - and the
// same mesh - as a cloud made with six.
template <typename Fn>
void forEachPuff(int count, int workers, Fn body) {
    if (workers <= 1 || count <= 1) {
        for (int i = 0; i < count; ++i) body(i, 0);
        return;
    }

    const int per = (count + workers - 1) / workers;
    std::vector<std::thread> pool;
    pool.reserve((size_t)workers);
    for (int w = 0; w < workers; ++w) {
        const int from = w * per;
        const int to = std::min(count, from + per);
        if (from >= to) continue;
        pool.emplace_back([&, from, to, w] {
            for (int i = from; i < to; ++i) body(i, w);
        });
    }
    for (std::thread& t : pool) t.join();
}

} // namespace

const char* cloudShapeName(CloudShape shape) {
    switch (shape) {
    case CloudShape::Cumulus: return "cloud.shape.cumulus";
    case CloudShape::Stratus: return "cloud.shape.stratus";
    case CloudShape::Cirrus: return "cloud.shape.cirrus";
    case CloudShape::Cumulonimbus: return "cloud.shape.cumulonimbus";
    }
    return "cloud.shape.cumulus";
}

const char* cloudModeName(CloudMode mode) {
    switch (mode) {
    case CloudMode::Flat: return "cloud.mode.flat";
    case CloudMode::Puffy: return "cloud.mode.puffy";
    }
    return "cloud.mode.puffy";
}

void Cloud::build(const CloudSettings& settings) {
    settings_ = settings;
    settings_.puffs = std::clamp(settings_.puffs, 1, kMaxPuffs);
    settings_.detail = std::clamp(settings_.detail, 1, 8);
    settings_.resolution = std::clamp(settings_.resolution, 3, 32);
    settings_.threads = std::clamp(settings_.threads, 1, 6);
    settings_.puffSize = std::fmax(0.05f, settings_.puffSize);
    settings_.spread = std::fmax(0.5f, settings_.spread);
    settings_.height = std::fmax(0.5f, settings_.height);
    settings_.coverage = clamp01(settings_.coverage);
    settings_.softness = clamp01(settings_.softness);

    puffs_.clear();
    puffs_.reserve((size_t)settings_.puffs);

    const bool flat = settings_.mode == CloudMode::Flat;
    const float halfWidth = settings_.spread * 0.5f;
    const float halfDepth = flat ? 0.0f : halfWidth * shapeDepth(settings_.shape);
    const float height = settings_.height;
    const Vec3 stretch = shapeStretch(settings_.shape);

    // How thick the sky has to be before it is cloud at all. The coverage is
    // how much of the sky the cloud takes, and this is the number that says
    // it: a high bar is a thin cloud with a fringe of small puffs, and a low
    // one is a cloud that fills the sky.
    const float threshold = 1.0f - settings_.coverage * 0.90f;

    // How much the puffs differ from one another. A soft cloud is a cloud of
    // balls of the same size, and a ragged one is a cloud of balls of every
    // size.
    const float vary = 0.25f + 0.75f * settings_.softness;

    for (int i = 0; i < settings_.puffs; ++i) {
        // The places this puff could stand in. The best one is taken: the
        // puffs of a cloud are in the thick parts of the noise and not in the
        // thin ones.
        float bestDensity = -1.0f;
        Vec3 bestCentre{};
        float bestRadius = settings_.puffSize;
        for (int place = 0; place < kPlacesPerPuff; ++place) {
            const unsigned int a = (unsigned int)(i * 16 + place);
            const float rx = hashUnit(a, 0, settings_.seed);
            const float ry = hashUnit(a, 1, settings_.seed);
            const float rz = hashUnit(a, 2, settings_.seed);
            const float x = (rx * 2.0f - 1.0f) * halfWidth;
            const float z = flat ? 0.0f : (rz * 2.0f - 1.0f) * halfDepth;
            const float y = ry * height;

            const float density = cloudDensity(settings_, x, y, z, halfWidth, halfDepth, height);

            // How big the puff is, as a part of the size that was asked for.
            // The edge of a cloud is a fringe of small puffs and not a wall,
            // so a puff in a thin part of the sky is a small one.
            const float fill = clamp01((density - threshold + 0.30f) / 0.60f);
            const float scale = 0.25f + 0.75f * fill;

            // And how much it differs from the others.
            const float jitter = 1.0f - 0.35f * vary + 0.70f * vary * hashUnit(a, 3, settings_.seed);
            float radius = settings_.puffSize * scale * jitter;

            // Too close to another puff: the puffs of a cloud overlap, but
            // two of them in the same place are one puff and a hole.
            bool crowded = false;
            for (const Puff& other : puffs_) {
                const Vec3 d = other.centre - Vec3{x, y, z};
                const float reach = (other.radius.x + radius) * 0.45f;
                if (d.lengthSq() < reach * reach) {
                    crowded = true;
                    break;
                }
            }
            if (crowded && place + 1 < kPlacesPerPuff) continue;

            if (density > bestDensity) {
                bestDensity = density;
                bestCentre = Vec3{x, y, z};
                bestRadius = radius;
            }
        }

        if (bestDensity < 0.0f) continue;

        // The bottom of a heap of cloud is a line, and the puffs that stand
        // on it are the ones that touch it.
        float centreY = bestCentre.y;
        const float halfHeight = bestRadius * stretch.y;
        if (shapeHasFlatBase(settings_.shape)) centreY = std::fmax(centreY, halfHeight * 0.85f);
        centreY = std::fmin(centreY, std::fmax(halfHeight * 0.5f, height - halfHeight * 0.5f));

        Puff puff;
        puff.centre = Vec3{bestCentre.x, centreY, bestCentre.z};
        // The discs of a flat cloud lie in the same plane, and two surfaces in
        // the same plane fight over the same pixel: the edge of one disc
        // flickers against the edge of the next. A fraction of a millimetre
        // apart settles it, and nobody can see that far apart.
        if (flat)
            puff.centre.z = (hashUnit((unsigned int)i, 7, settings_.seed) - 0.5f) * 0.02f;
        puff.radius = Vec3{bestRadius * stretch.x, halfHeight, bestRadius * stretch.z};
        // How deep in the cloud it is: a puff in a thick part of the noise is
        // a puff in the middle of the cloud, and the middle of a cloud is
        // grey.
        puff.buried = bestDensity;
        puffs_.push_back(puff);
    }
}

MeshAsset Cloud::toMesh() const {
    MeshAsset asset;
    asset.name = "cloud";
    if (!ready()) return asset;

    const int count = (int)puffs_.size();
    const int workers = std::clamp(settings_.threads, 1, 6);
    const int rings = std::max(2, settings_.resolution / 2);

    // The puffs are made in parallel and each thread writes into a chunk of
    // its own: nothing is shared, nothing is locked, and the puffs come out
    // in the same order whatever order they were made in.
    std::vector<Chunk> chunks((size_t)workers);
    forEachPuff(count, workers, [&](int index, int chunk) {
        Chunk& target = chunks[(size_t)chunk];
        const Puff& puff = puffs_[(size_t)index];
        if (settings_.mode == CloudMode::Flat)
            addDisc(target, puff, settings_.resolution, settings_);
        else
            addSphere(target, puff, settings_.resolution, rings, settings_);
    });

    // The chunks are put together into one part: a cloud is one draw call and
    // not one call per puff.
    MeshPart part;
    size_t vertexTotal = 0;
    size_t indexTotal = 0;
    for (const Chunk& chunk : chunks) {
        vertexTotal += chunk.vertices.size() / kMeshVertexFloats;
        indexTotal += chunk.indices.size();
    }
    part.vertices.reserve(vertexTotal * kMeshVertexFloats);
    part.indices.reserve(indexTotal);
    part.colors.reserve(vertexTotal * 3);

    unsigned int base = 0;
    for (const Chunk& chunk : chunks) {
        part.vertices.insert(part.vertices.end(), chunk.vertices.begin(), chunk.vertices.end());
        part.colors.insert(part.colors.end(), chunk.colors.begin(), chunk.colors.end());
        for (unsigned int index : chunk.indices) part.indices.push_back(base + index);
        base += (unsigned int)(chunk.vertices.size() / kMeshVertexFloats);
    }

    // The bounds of the cloud, measured over what was made: the editor draws
    // a box around the body and the physics uses the half sizes.
    Vec3 low{1e9f, 1e9f, 1e9f};
    Vec3 high{-1e9f, -1e9f, -1e9f};
    for (size_t i = 0; i < part.vertices.size(); i += kMeshVertexFloats) {
        const float x = part.vertices[i + kMeshPositionOffset + 0];
        const float y = part.vertices[i + kMeshPositionOffset + 1];
        const float z = part.vertices[i + kMeshPositionOffset + 2];
        low.x = std::fmin(low.x, x);
        low.y = std::fmin(low.y, y);
        low.z = std::fmin(low.z, z);
        high.x = std::fmax(high.x, x);
        high.y = std::fmax(high.y, y);
        high.z = std::fmax(high.z, z);
    }
    asset.boundsMin = low;
    asset.boundsMax = high;
    asset.center = (asset.boundsMin + asset.boundsMax) * 0.5f;
    asset.half = (asset.boundsMax - asset.boundsMin) * 0.5f;
    asset.radius = asset.half.length();
    asset.scale = 1.0f;

    // The colour of the part is white: the colour of the cloud is in the
    // vertices, and a tint over it would take it away again.
    part.color[0] = 1.0f;
    part.color[1] = 1.0f;
    part.color[2] = 1.0f;

    asset.parts.push_back(std::move(part));
    return asset;
}

} // namespace s3d
