#include "scene/Noise.hpp"

#include <algorithm>
#include <cmath>

namespace s3d {
namespace {

// A number in 0..1 for a point in the plane. The sine is the old trick: it is
// not a good hash, but it is a good enough one, and a picture does not need a
// better one.
float hash(float x, float y, unsigned seed) {
    const float h = std::sin(x * 127.1f + y * 311.7f + (float)seed * 0.0137f) * 43758.5453f;
    return h - std::floor(h);
}

// The smoothstep: the difference between a grid of squares and a cloud.
float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

} // namespace

float valueNoise(float x, float y, unsigned seed) {
    const float ix = std::floor(x);
    const float iy = std::floor(y);
    const float fx = smooth(x - ix);
    const float fy = smooth(y - iy);

    const float a = hash(ix, iy, seed);
    const float b = hash(ix + 1.0f, iy, seed);
    const float c = hash(ix, iy + 1.0f, seed);
    const float d = hash(ix + 1.0f, iy + 1.0f, seed);

    const float top = a + (b - a) * fx;
    const float bottom = c + (d - c) * fx;
    return top + (bottom - top) * fy;
}

float fbm(float x, float y, unsigned seed, int octaves) {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float total = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += valueNoise(x, y, seed + (unsigned)i * 17u) * amplitude;
        total += amplitude;
        x *= 2.0f;
        y *= 2.0f;
        amplitude *= 0.5f;
    }
    return total > 0.0f ? sum / total : 0.0f;
}

} // namespace s3d
