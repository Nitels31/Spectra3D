#include "scene/Noise.hpp"

#include <algorithm>
#include <cmath>

namespace s3d {
namespace {

// Eight directions: the diagonals and the sides. Perlin's own set has twelve,
// and the extra four are worth the arithmetic only in three dimensions.
constexpr float kSqrtHalf = 0.70710678f;
constexpr float kGradX[8] = {1.0f, -1.0f, 0.0f, 0.0f, kSqrtHalf, -kSqrtHalf, kSqrtHalf, -kSqrtHalf};
constexpr float kGradY[8] = {0.0f, 0.0f, 1.0f, -1.0f, kSqrtHalf, kSqrtHalf, -kSqrtHalf, -kSqrtHalf};

// The twelve edges of a cube: the gradients of a lattice point in three
// dimensions.
//
// The eight directions of the flat noise cannot be used here - a noise made of
// them has a grain along the axis that has no direction of its own, and the
// third dimension of a cloud is exactly where the eye looks for the grain.
// The edges of the cube have none: every one of the twelve has a part in all
// three axes.
constexpr float kSqrtTwo = 1.41421356f;
constexpr float kGrad3[12][3] = {
    {1.0f / kSqrtTwo, 1.0f / kSqrtTwo, 0.0f},  {-1.0f / kSqrtTwo, 1.0f / kSqrtTwo, 0.0f},
    {1.0f / kSqrtTwo, -1.0f / kSqrtTwo, 0.0f}, {-1.0f / kSqrtTwo, -1.0f / kSqrtTwo, 0.0f},
    {1.0f / kSqrtTwo, 0.0f, 1.0f / kSqrtTwo},  {-1.0f / kSqrtTwo, 0.0f, 1.0f / kSqrtTwo},
    {1.0f / kSqrtTwo, 0.0f, -1.0f / kSqrtTwo}, {-1.0f / kSqrtTwo, 0.0f, -1.0f / kSqrtTwo},
    {0.0f, 1.0f / kSqrtTwo, 1.0f / kSqrtTwo},  {0.0f, -1.0f / kSqrtTwo, 1.0f / kSqrtTwo},
    {0.0f, 1.0f / kSqrtTwo, -1.0f / kSqrtTwo}, {0.0f, -1.0f / kSqrtTwo, -1.0f / kSqrtTwo},
};

// The curve between two lattice points.
//
// The smoothstep (3t^2 - 2t^3) is continuous but its slope jumps at the
// lattice points, and a jump in the slope is a crease in the ground - a line
// you can see across a hill. The quintic has no jump in its slope either, so
// the ground has no creases in it at all.
float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// The seed of one octave. A stride that is not a small number: two octaves
// with seeds next to each other would share their lattice and the noise would
// come out with the same shape in both.
constexpr unsigned int kOctaveStride = 1013u;

} // namespace

unsigned int hashLattice(int x, int y, unsigned int seed) {
    // A 32 bit mix of three integers. The multiply-xor-shift chain at the end
    // is what makes it avalanche: without it, neighbouring lattice points give
    // neighbouring hashes, and neighbouring hashes give a noise that has a
    // grain in it - stripes along one axis.
    unsigned int h = (unsigned int)x * 0x8DA6B343u;
    h ^= (unsigned int)y * 0xD8163841u;
    h += seed * 0xCB1AB31Fu;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

void latticeGradient(int x, int y, unsigned int seed, float& gx, float& gy) {
    const unsigned int h = hashLattice(x, y, seed);
    const int index = (int)(h & 7u);
    gx = kGradX[index];
    gy = kGradY[index];
}

float perlinNoise(float x, float y, unsigned int seed) {
    const int xi = (int)std::floor(x);
    const int yi = (int)std::floor(y);
    const float xf = x - (float)xi;
    const float yf = y - (float)yi;
    const float u = fade(xf);
    const float v = fade(yf);

    float gx = 0.0f, gy = 0.0f;

    latticeGradient(xi, yi, seed, gx, gy);
    const float n00 = gx * xf + gy * yf;
    latticeGradient(xi + 1, yi, seed, gx, gy);
    const float n10 = gx * (xf - 1.0f) + gy * yf;
    latticeGradient(xi, yi + 1, seed, gx, gy);
    const float n01 = gx * xf + gy * (yf - 1.0f);
    latticeGradient(xi + 1, yi + 1, seed, gx, gy);
    const float n11 = gx * (xf - 1.0f) + gy * (yf - 1.0f);

    const float a = n00 + u * (n10 - n00);
    const float b = n01 + u * (n11 - n01);
    // The factor puts the result back into -1..1: the raw sum of four dot
    // products of unit vectors cannot reach the ends of the range, and a noise
    // that never does is a noise with a flat spot in the middle of its range.
    return (a + v * (b - a)) * 1.41421356f;
}

float fbm(float x, float y, unsigned int seed, int octaves, float lacunarity, float gain) {
    if (octaves < 1) octaves = 1;
    float sum = 0.0f;
    float amplitude = 1.0f;
    float total = 0.0f;
    float fx = x, fy = y;
    for (int i = 0; i < octaves; ++i) {
        sum += perlinNoise(fx, fy, seed + (unsigned int)i * kOctaveStride) * amplitude;
        total += amplitude;
        fx *= lacunarity;
        fy *= lacunarity;
        amplitude *= gain;
    }
    if (total <= 0.0f) return 0.5f;
    return clamp01((sum / total) * 0.5f + 0.5f);
}

float ridgedFbm(float x, float y, unsigned int seed, int octaves, float lacunarity, float gain) {
    if (octaves < 1) octaves = 1;
    float sum = 0.0f;
    float amplitude = 1.0f;
    float total = 0.0f;
    float fx = x, fy = y;
    // How much of the next octave is allowed through. A ridge that is not
    // held back by the one below it is a ridge with the same size everywhere,
    // and the mountain has one scale instead of several.
    float weight = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        float n = perlinNoise(fx, fy, seed + (unsigned int)i * kOctaveStride);
        // The fold: the zero line of the noise becomes a crest.
        n = 1.0f - std::fabs(n);
        n *= n;
        n *= weight;
        weight = clamp01(n * 2.0f);
        sum += n * amplitude;
        total += amplitude;
        fx *= lacunarity;
        fy *= lacunarity;
        amplitude *= gain;
    }
    if (total <= 0.0f) return 0.0f;
    return clamp01(sum / total);
}

float billowFbm(float x, float y, unsigned int seed, int octaves, float lacunarity, float gain) {
    if (octaves < 1) octaves = 1;
    float sum = 0.0f;
    float amplitude = 1.0f;
    float total = 0.0f;
    float fx = x, fy = y;
    for (int i = 0; i < octaves; ++i) {
        const float n = std::fabs(perlinNoise(fx, fy, seed + (unsigned int)i * kOctaveStride));
        sum += (n * 2.0f - 1.0f) * amplitude;
        total += amplitude;
        fx *= lacunarity;
        fy *= lacunarity;
        amplitude *= gain;
    }
    if (total <= 0.0f) return 0.5f;
    return clamp01((sum / total) * 0.5f + 0.5f);
}

float cellularNoise(float x, float y, unsigned int seed) {
    const int xi = (int)std::floor(x);
    const int yi = (int)std::floor(y);
    float best = 1e9f;
    // The nine cells around the point. A point in the corner of its cell is
    // closest to one of the three cells across the border, so three by three is
    // enough - the ones further away cannot be the nearest.
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            const int cx = xi + i;
            const int cy = yi + j;
            const unsigned int h = hashLattice(cx, cy, seed);
            // The point's place inside its cell, from the high and the low
            // halves of the hash.
            const float px = (float)cx + (float)(h & 0xFFFFu) / 65535.0f;
            const float py = (float)cy + (float)((h >> 16) & 0xFFFFu) / 65535.0f;
            const float dx = px - x;
            const float dy = py - y;
            const float d = dx * dx + dy * dy;
            if (d < best) best = d;
        }
    }
    return clamp01(std::sqrt(best));
}

float warpedFbm(float x, float y, unsigned int seed, int octaves, float strength) {
    if (strength <= 0.0f) return fbm(x, y, seed, octaves);

    // Two noises, then two more noises that read the first two, and the
    // coordinates are moved by the result. Every layer bends the shapes of
    // the layers above it, and the edges that would have been straight are
    // folded instead.
    const float q1 = fbm(x, y, seed + 7919u, 3);
    const float q2 = fbm(x + 5.2f, y + 1.3f, seed + 104729u, 3);
    const float r1 = fbm(x + 4.0f * q1 + 1.7f, y + 4.0f * q2 + 9.2f, seed + 1299709u, 3);
    const float r2 = fbm(x + 4.0f * q1 + 8.3f, y + 4.0f * q2 + 2.8f, seed + 15485863u, 3);
    return fbm(x + strength * r1, y + strength * r2, seed, octaves);
}

unsigned int hashLattice3(int x, int y, int z, unsigned int seed) {
    // The same mix as in two dimensions, with the third axis mixed in as well.
    // The three are mixed in one after the other and not combined into one
    // number first: combining them would make (1,2,3) and (3,2,1) the same
    // point, and a cloud that is the same cloud from two sides is a cloud
    // with a mirror in it.
    unsigned int h = (unsigned int)x * 0x8DA6B343u;
    h ^= (unsigned int)y * 0xD8163841u;
    h ^= (unsigned int)z * 0xCB1AB31Fu;
    h += seed * 0x165667B1u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

void latticeGradient3(int x, int y, int z, unsigned int seed, float& gx, float& gy, float& gz) {
    const unsigned int h = hashLattice3(x, y, z, seed);
    const int index = (int)(h % 12u);
    gx = kGrad3[index][0];
    gy = kGrad3[index][1];
    gz = kGrad3[index][2];
}

float perlinNoise3(float x, float y, float z, unsigned int seed) {
    const int xi = (int)std::floor(x);
    const int yi = (int)std::floor(y);
    const int zi = (int)std::floor(z);
    const float xf = x - (float)xi;
    const float yf = y - (float)yi;
    const float zf = z - (float)zi;
    const float u = fade(xf);
    const float v = fade(yf);
    const float w = fade(zf);

    // The eight corners of the cube the point is in, and what the gradient at
    // each of them says about the way to it.
    static const int kDx[8] = {0, 1, 0, 1, 0, 1, 0, 1};
    static const int kDy[8] = {0, 0, 1, 1, 0, 0, 1, 1};
    static const int kDz[8] = {0, 0, 0, 0, 1, 1, 1, 1};
    float corner[8];
    float gx = 0.0f, gy = 0.0f, gz = 0.0f;
    for (int i = 0; i < 8; ++i) {
        latticeGradient3(xi + kDx[i], yi + kDy[i], zi + kDz[i], seed, gx, gy, gz);
        corner[i] =
            gx * (xf - (float)kDx[i]) + gy * (yf - (float)kDy[i]) + gz * (zf - (float)kDz[i]);
    }

    // The same blending as in two dimensions, one axis at a time.
    const float x00 = corner[0] + u * (corner[1] - corner[0]);
    const float x10 = corner[2] + u * (corner[3] - corner[2]);
    const float x01 = corner[4] + u * (corner[5] - corner[4]);
    const float x11 = corner[6] + u * (corner[7] - corner[6]);
    const float y0 = x00 + v * (x10 - x00);
    const float y1 = x01 + v * (x11 - x01);
    // The factor puts the result back into -1..1: the raw sum of eight dot
    // products of unit vectors cannot reach the ends of the range.
    const float value = (y0 + w * (y1 - y0)) * 1.15470054f;
    return value < -1.0f ? -1.0f : (value > 1.0f ? 1.0f : value);
}

float fbm3(float x, float y, float z, unsigned int seed, int octaves, float lacunarity,
           float gain) {
    if (octaves < 1) octaves = 1;
    float sum = 0.0f;
    float amplitude = 1.0f;
    float total = 0.0f;
    float fx = x, fy = y, fz = z;
    for (int i = 0; i < octaves; ++i) {
        sum += perlinNoise3(fx, fy, fz, seed + (unsigned int)i * kOctaveStride) * amplitude;
        total += amplitude;
        fx *= lacunarity;
        fy *= lacunarity;
        fz *= lacunarity;
        amplitude *= gain;
    }
    if (total <= 0.0f) return 0.5f;
    return clamp01((sum / total) * 0.5f + 0.5f);
}

} // namespace s3d
