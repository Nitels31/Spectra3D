// skygen - the sky the engine ships.
//
// The engine has no shaders: it draws with the fixed-function pipeline, and a
// skybox there is a picture. This is where the picture comes from.
//
// It writes an equirectangular picture - the sky in one piece, twice as wide
// as it is tall - which the engine turns into a cube map when it loads it.
// The picture is made once and committed next to the engine's other assets, so
// that nothing has to be changed to get a sky: `assets/sky.jpg` is the sky a
// scene gets.
//
// Usage:
//   skygen [output] [width] [height]
//
// The default is `assets/sky.jpg` at 4096x2048. That size is not arbitrary: a
// quarter of the picture's width is one face of the cube map the engine builds
// from it, so 4096 gives 1024x1024 faces - enough that the sky is sharp
// without a file nobody wants to download.
//
// The formulas that place a direction in the picture are the ones in
// src/render/Skybox.cpp, and the two have to agree: the engine reads
// u = atan2(z, x) / 2pi + 0.5 and v = 0.5 - asin(y) / pi, and this file
// writes the other way round.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace {

constexpr float kPi = 3.14159265358979f;

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct Rgb {
    float r = 0.0f, g = 0.0f, b = 0.0f;
};

float saturate(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

float mixf(float a, float b, float t) { return a + (b - a) * t; }

Rgb mixRgb(const Rgb& a, const Rgb& b, float t) {
    return Rgb{mixf(a.r, b.r, t), mixf(a.g, b.g, t), mixf(a.b, b.b, t)};
}

Rgb addRgb(const Rgb& a, const Rgb& b) { return Rgb{a.r + b.r, a.g + b.g, a.b + b.b}; }

Rgb scaleRgb(const Rgb& a, float s) { return Rgb{a.r * s, a.g * s, a.b * s}; }

float smoothstep(float edge0, float edge1, float x) {
    const float t = saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

float dot3(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// A hash with nothing in it but arithmetic: no table, no state, and the same
// answer on every machine.
float hash21(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= (h >> 16);
    return (float)(h & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

// Value noise: the hash at the four corners of a cell, interpolated with the
// smoothstep so that the cells do not show their edges.
float valueNoise(float x, float y) {
    const int xi = (int)std::floor(x);
    const int yi = (int)std::floor(y);
    const float xf = x - (float)xi;
    const float yf = y - (float)yi;
    const float u = xf * xf * (3.0f - 2.0f * xf);
    const float v = yf * yf * (3.0f - 2.0f * yf);
    const float a = hash21(xi, yi);
    const float b = hash21(xi + 1, yi);
    const float c = hash21(xi, yi + 1);
    const float d = hash21(xi + 1, yi + 1);
    const float bottom = a + (b - a) * u;
    const float top = c + (d - c) * u;
    return bottom + (top - bottom) * v;
}

// Five layers of noise, each half the size and half the strength of the one
// below. The layers are moved sideways as well as scaled, so they do not line
// up into a grid.
float fbm(float x, float y) {
    float sum = 0.0f;
    float amplitude = 0.5f;
    float fx = x, fy = y;
    for (int i = 0; i < 5; ++i) {
        sum += valueNoise(fx, fy) * amplitude;
        fx = fx * 2.03f + 17.3f;
        fy = fy * 2.03f - 9.1f;
        amplitude *= 0.5f;
    }
    return sum * 1.032f;  // the sum of the amplitudes, so the range is 0..1
}

// The sun. Somewhere in the west and a fifth of the way up, so that a scene
// opened in the default camera looks into it and the world has a light to
// come from.
const Vec3 kSun{-0.30f, 0.36f, -0.88f};

// The palette. A deep blue at the zenith, a pale band at the horizon, a warm
// glow around the sun and a dark ground below - the dark ground is what lets
// the world's own colours, the grid and the bodies, be read against the sky.
const Rgb kZenith{0.11f, 0.27f, 0.72f};
const Rgb kHorizon{0.64f, 0.77f, 0.94f};
const Rgb kSunTint{1.00f, 0.88f, 0.66f};
const Rgb kSunDisc{1.00f, 0.98f, 0.92f};
const Rgb kCloudLit{1.00f, 0.99f, 0.97f};
const Rgb kCloudShadow{0.50f, 0.55f, 0.64f};
const Rgb kGround{0.11f, 0.12f, 0.12f};
const Rgb kHaze{0.56f, 0.64f, 0.75f};

// The colour of the sky in one direction.
Rgb skyColour(const Vec3& d) {
    const float mu = dot3(d, kSun);
    const float up = d.y;

    if (up >= 0.0f) {
        // Above the horizon. The exponent puts the blue where a person looks
        // rather than right at the edge of it: the blue starts a few degrees
        // above the horizon, and the pale band is the last thin strip before
        // it.
        const Rgb base = mixRgb(kHorizon, kZenith, std::pow(saturate(up), 0.42f));
        // Mie scattering: the sky around the sun is brighter and warmer, and
        // the glow reaches much further than the disc itself.
        const float glow = std::pow(saturate(mu), 6.0f) * 0.26f +
                           std::pow(saturate(mu), 64.0f) * 0.55f;
        Rgb colour = addRgb(base, scaleRgb(kSunTint, glow));
        // The disc. Half a degree across, which is about what the sun is.
        const float disc = smoothstep(std::cos(0.55f * kPi / 180.0f),
                                      std::cos(0.30f * kPi / 180.0f), mu);
        colour = mixRgb(colour, kSunDisc, disc);

        // The cloud deck. A flat layer of cloud, so the direction is projected
        // onto it and the clouds stretch towards the horizon the way real ones
        // do. The projection is what makes the deck look like a deck and not
        // like a wallpaper.
        const float plane = 1.0f / std::max(up, 0.055f);
        const float cx = d.x * plane * 0.85f + 4.3f;
        const float cy = d.z * plane * 0.85f - 2.1f;
        const float noise = fbm(cx, cy);
        float cover = smoothstep(0.46f, 0.70f, noise);
        // The deck thins towards the horizon, where it is seen edge on and
        // every cloud is a kilometre away.
        cover *= smoothstep(0.0f, 0.20f, up);
        // The side of a cloud that faces the sun is bright; the rest is in the
        // shadow of the cloud above it.
        const float light = 0.60f + 0.40f * std::pow(saturate(mu), 3.0f);
        return mixRgb(colour, mixRgb(kCloudShadow, kCloudLit, light), cover);
    }

    // Below the horizon: the ground, fading into the haze at the horizon line.
    const float t = std::pow(saturate(-up), 0.5f);
    Rgb ground = mixRgb(kHaze, kGround, t);
    // A little texture, so the ground is a surface and not a fill. The grain is
    // scaled by the depth: near the horizon the direction stretches, and the
    // same noise would draw vertical streaks down the picture.
    const float grain =
        0.94f + 0.12f * t * fbm(d.x * 6.0f + 30.0f, d.z * 6.0f - 12.0f);
    return scaleRgb(ground, grain);
}

} // namespace

int main(int argc, char** argv) {
    std::string output = "assets/sky.jpg";
    int width = 4096;
    int height = 2048;

    if (argc > 1) output = argv[1];
    if (argc > 2) width = std::atoi(argv[2]);
    if (argc > 3) height = std::atoi(argv[3]);
    if (width < 16 || height < 8) {
        std::fprintf(stderr, "skygen: the picture has to be at least 16x8\n");
        return 2;
    }

    std::vector<unsigned char> pixels((size_t)width * (size_t)height * 3);

    for (int y = 0; y < height; ++y) {
        // The top row is the zenith, the bottom row the nadir.
        const float v = ((float)y + 0.5f) / (float)height;
        const float theta = (0.5f - v) * kPi;
        const float cosTheta = std::cos(theta);
        const float sinTheta = std::sin(theta);
        for (int x = 0; x < width; ++x) {
            const float u = ((float)x + 0.5f) / (float)width;
            const float phi = (u - 0.5f) * 2.0f * kPi;
            const Vec3 direction{cosTheta * std::cos(phi), sinTheta,
                                 cosTheta * std::sin(phi)};
            const Rgb colour = skyColour(direction);
            const size_t at = ((size_t)y * (size_t)width + (size_t)x) * 3;
            pixels[at + 0] = (unsigned char)(saturate(colour.r) * 255.0f + 0.5f);
            pixels[at + 1] = (unsigned char)(saturate(colour.g) * 255.0f + 0.5f);
            pixels[at + 2] = (unsigned char)(saturate(colour.b) * 255.0f + 0.5f);
        }
    }

    const bool isJpeg = (output.size() > 4 && output.compare(output.size() - 4, 4, ".jpg") == 0) ||
                        (output.size() > 5 && output.compare(output.size() - 5, 5, ".jpeg") == 0);
    int written = 0;
    if (isJpeg) {
        // 92 is the point where the picture stops getting smaller and starts
        // getting blotchy around the sun.
        written = stbi_write_jpg(output.c_str(), width, height, 3, pixels.data(), 92);
    } else {
        written = stbi_write_png(output.c_str(), width, height, 3, pixels.data(),
                                 width * 3);
    }
    if (!written) {
        std::fprintf(stderr, "skygen: cannot write '%s'\n", output.c_str());
        return 1;
    }

    std::printf("skygen: wrote %s (%dx%d)\n", output.c_str(), width, height);
    return 0;
}
