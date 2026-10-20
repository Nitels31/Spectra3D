#pragma once
#include <vector>

#include <GL/gl.h>

#include "core/Math.hpp"

namespace s3d {

// How the shadows are drawn.
enum class ShadowQuality {
    Off,      // no shadows at all
    Hard,     // one tap: the edge is a stair, and it costs one read
    Filtered, // a few taps in a disk: the edge is soft over a texel or two
    Soft,     // the blockers are looked for first: a shadow with a width
};

// The shadows, as the user sets them.
struct ShadowSettings {
    // The quality. One tap is the cheapest and a scene with a hard sun has a
    // hard shadow; the filtered one is the default, because a stair-stepped
    // edge is the first thing the eye picks out of a picture.
    ShadowQuality quality = ShadowQuality::Filtered;

    // The size of the map, in texels a side.
    int resolution = 2048;

    // How many stretches of the scene get a map of their own. Four is the
    // default: one map for the whole scene means forty metres of ground and
    // a box of one metre share the same texels, and the shadow of the box is
    // then a smear.
    int cascades = 4;

    // The sun. The azimuth is around the vertical axis, the elevation is above
    // the horizon, and both are in degrees.
    //
    // They are the numbers the fixed pipeline's light had, so that the picture
    // is the picture it was before the shadows came: the same light, the same
    // direction, and the shadow is the only thing that is new.
    float sunAzimuth = 41.2f;
    float sunElevation = 58.6f;

    // The bias of the comparison, in the world's own units: how far the
    // surface is pushed towards the light before it is compared with the map.
    //
    // In world units and not in the map's, because the map's own units change
    // with the size of the map and with the stretch of the scene it covers: a
    // bias that is right for a 2048 map is twice as big for a 4096 one, and
    // the shadow lets go of the thing that casts it. A number in metres means
    // the same thing in every picture.
    float bias = 0.015f;

    // How far the surface is pushed along its own normal, in texels of the
    // map, before the comparison.
    //
    // This is the part of the bias that does not make the shadow let go: the
    // surface is moved along the direction it faces, so it keeps touching the
    // thing that casts its shadow, and only the acne - the stripes a surface
    // draws on itself - is gone. Two texels is enough for the map as it is.
    float normalOffset = 2.0f;

    // The widest the penumbra of the soft mode may get, in texels.
    float radius = 12.0f;

    // The light: the sun's own colour, the sky's and the ground's bounce.
    //
    // They are the numbers the fixed pipeline's light had, so that a scene
    // without shadows looks exactly as it did before.
    Vec3 sunColour{0.95f, 0.94f, 0.90f};
    Vec3 skyColour{0.26f, 0.27f, 0.31f};
    Vec3 groundColour{0.20f, 0.19f, 0.18f};
    float ambient = 1.0f;
    // The fixed pipeline had no shine at all, and a scene that used to have
    // none should not grow one by accident.
    float specular = 0.0f;
    float shininess = 32.0f;

    // The haze: the colour of the far things and how fast they lose it. The
    // fixed pipeline had none, so it is off until somebody asks for it.
    Vec3 fogColour{0.62f, 0.72f, 0.86f};
    float fogDensity = 0.0f;
};

// The direction of the sun, from the settings: the point where the light is.
Vec3 sunDirection(const ShadowSettings& settings);

// The shadow map: the picture of how far away the sun hit something first.
//
// One texture, one layer per cascade. The distance in the map is the distance
// in the world, because a directional light has an orthographic projection -
// so a shadow is one comparison of two numbers that mean the same thing.
class ShadowMap {
public:
    ~ShadowMap();

    // Creates the map. The size and the number of the layers come from the
    // settings, and a change to either of them makes the map again.
    bool initialize(const ShadowSettings& settings);
    void setSettings(const ShadowSettings& settings);
    const ShadowSettings& settings() const { return settings_; }

    bool ready() const { return framebuffer_ != 0; }
    GLuint texture() const { return texture_; }
    int resolution() const { return resolution_; }
    int layers() const { return layers_; }

    // A number that changes every time the map is made again.
    //
    // The map is a picture, and a map that was made again holds nothing: the
    // renderer has to draw it again, and the only way to know that it has to
    // is to be told that the picture it has is a new one.
    unsigned int generation() const { return generation_; }

    // The matrix of every cascade and where the stretches of the scene end.
    // The shader picks the cascade by the distance from the middle of the
    // scene and multiplies by this one matrix.
    const std::vector<Mat4>& lightMatrices() const { return lightMatrices_; }
    const std::vector<float>& cascadeSplits() const { return cascadeSplits_; }
    // The middle of the scene and the radius of each stretch of it.
    const Vec3& sceneCentre() const { return sceneCentre_; }
    const std::vector<float>& cascadeRadii() const { return cascadeRadii_; }

    // How deep each stretch of the scene is, in the world's own units: the
    // distance from the light to the far end of its box.
    //
    // The shader needs it because the bias is a distance in metres and the
    // map holds a distance as a fraction of one: the same bias in every
    // stretch means a different number in the map, and the stretch that
    // covers the whole scene needs the smallest one.
    const std::vector<float>& cascadeDepthRanges() const { return cascadeDepthRanges_; }
    // How big one texel of each stretch is, in the world's own units. The
    // filter and the bias are both counted in texels, and a texel of the
    // near stretch is a hundredth of a texel of the far one.
    const std::vector<float>& cascadeTexelSizes() const { return cascadeTexelSizes_; }

    // The two ways the map is read: the texels where they are, and the texels
    // spread out.
    //
    // They are two sampler objects and not one texture read two ways, because
    // the filter belongs to the texture and not to the place it is read from:
    // one texture bound to two units is filtered the same in both.
    GLuint pointSampler() const { return pointSampler_; }
    GLuint linearSampler() const { return linearSampler_; }

    // The pass: the map is filled with the distance from the sun.
    void beginPass(int cascade);
    void endPass();

    // Reads one stretch of the map back, in the map's own units: the distance
    // from the light, as a fraction of how deep the box of that stretch is.
    //
    // It is here for the tools that look at the shadows - the one thing worth
    // seeing when the shadows are not what they should be is the map they are
    // read from - and it is a read and not a copy: the picture is where it is,
    // and the tools ask it for the numbers.
    bool readLayer(int cascade, std::vector<unsigned short>& out) const;

    // Fits the box of every cascade to the scene.
    //
    // The scene and not the view: the map is a picture of the world as the sun
    // sees it, and the sun does not care where the eye is. A map fitted to the
    // view is a different picture every time the eye moves, and then it has to
    // be drawn again every time the eye moves - which is every frame of a
    // flight, and the whole point of the map was to stop that.
    //
    // The centre of the scene and its radius are what the cascades are cut
    // from: the first is the middle of everything that casts, and the rest are
    // the halves of it, so that what is in the middle of the scene gets the
    // texels and what is at the edge does not steal them.
    void fit(const Vec3& centre, float radius);

private:
    // Creates the texture and the framebuffer that draws into it.
    bool create(int resolution, int layers);
    void destroy();

    // Fits the box of one cascade to the sphere of the scene it covers.
    void fitCascade(int cascade, const Vec3& centre, float radius);

    ShadowSettings settings_;
    unsigned int texture_ = 0;
    unsigned int framebuffer_ = 0;
    unsigned int depthBuffer_ = 0;
    // The two readings of the map. Sampler objects, because the filter of a
    // texture is the texture's own and not the place it is read from.
    unsigned int pointSampler_ = 0;
    unsigned int linearSampler_ = 0;
    int resolution_ = 0;
    int layers_ = 0;
    // How many times the map has been made. The renderer watches it.
    unsigned int generation_ = 0;
    std::vector<Mat4> lightMatrices_;
    std::vector<float> cascadeSplits_;
    // The middle of the scene, and the radius of each stretch of it.
    Vec3 sceneCentre_;
    std::vector<float> cascadeRadii_;
    // How deep each stretch is and how big one of its texels is, in metres.
    std::vector<float> cascadeDepthRanges_;
    std::vector<float> cascadeTexelSizes_;
};

} // namespace s3d
