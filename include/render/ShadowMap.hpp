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
    // The quality. One tap is the default: a scene with a hard sun has a hard
    // shadow, and the soft ones cost sixteen reads a fragment.
    ShadowQuality quality = ShadowQuality::Hard;

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

    // The bias of the comparison, in the map's own units. Too little and the
    // ground is striped by its own shadow; too much and the shadow lets go of
    // the thing that casts it.
    float bias = 0.0006f;

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

    // The matrix of every cascade and where the stretches of the scene end.
    // The shader picks the cascade by the distance from the eye and multiplies
    // by this one matrix.
    const std::vector<Mat4>& lightMatrices() const { return lightMatrices_; }
    const std::vector<float>& cascadeSplits() const { return cascadeSplits_; }

    // The pass: the map is filled with the distance from the sun.
    void beginPass(int cascade);
    void endPass();

    // Fits the box of every cascade to the slice of the view it covers.
    //
    // The bodies are not what decides it. A ground of forty metres and a box
    // of one metre in the same map means that the box's shadow is a smear of
    // fifty texels, and a shadow has to be sharper than that to be worth
    // looking at.
    //
    // The eye, where it looks and how wide it looks are what decide it: the
    // map covers what is on the screen, and the texels are spent there.
    void fit(const Vec3& eye, const Vec3& forward, const Vec3& right, const Vec3& up,
             float halfHeight, float aspect, float nearPlane, float farPlane);

private:
    // Creates the texture and the framebuffer that draws into it.
    bool create(int resolution, int layers);
    void destroy();

    // Fits the box of one cascade to the slice of the view it covers.
    void fitCascade(int cascade, const Vec3& eye, const Vec3& forward, const Vec3& right,
                    const Vec3& up, float halfHeight, float aspect);

    ShadowSettings settings_;
    unsigned int texture_ = 0;
    unsigned int framebuffer_ = 0;
    unsigned int depthBuffer_ = 0;
    int resolution_ = 0;
    int layers_ = 0;
    std::vector<Mat4> lightMatrices_;
    std::vector<float> cascadeSplits_;
};

} // namespace s3d
