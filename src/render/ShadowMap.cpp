#include "render/ShadowMap.hpp"

#include "core/Log.hpp"
#include "platform/GlFunctions.hpp"

#include <algorithm>
#include <cmath>

namespace s3d {
namespace {

// The nearest the light's box reaches, in the map's own units. The light is
// put far enough away that nothing is behind it, and this is the margin.
constexpr float kLightMargin = 10.0f;

// The box of a cascade that has nothing in it: a small one around the eye, so
// that the shader has a matrix and the map is not empty.
constexpr float kEmptyBox = 8.0f;

} // namespace

Vec3 sunDirection(const ShadowSettings& settings) {
    const float azimuth = settings.sunAzimuth * kPi / 180.0f;
    const float elevation = settings.sunElevation * kPi / 180.0f;
    const float horizontal = std::cos(elevation);
    // The point the light is in, and not the direction the light travels: the
    // sun is above and to the side, and the light comes down from there.
    return Vec3{std::sin(azimuth) * horizontal, std::sin(elevation),
                std::cos(azimuth) * horizontal}
        .normalized();
}

ShadowMap::~ShadowMap() { destroy(); }

void ShadowMap::destroy() {
    if (framebuffer_) gl::DeleteFramebuffers(1, &framebuffer_);
    if (depthBuffer_) gl::DeleteRenderbuffers(1, &depthBuffer_);
    if (texture_) glDeleteTextures(1, &texture_);
    framebuffer_ = 0;
    depthBuffer_ = 0;
    texture_ = 0;
    resolution_ = 0;
    layers_ = 0;
}

bool ShadowMap::create(int resolution, int layers) {
    destroy();

    // The map is a picture of distances: one channel, and the other three are
    // wasted space. Sixteen bits is enough - the distance is measured in the
    // light's own units over a range of a few hundred metres, and a
    // millimetre of it is more than the comparison needs.
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture_);
    gl::TexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R16, resolution, resolution, layers, 0, GL_RED,
                 GL_UNSIGNED_SHORT, nullptr);
    // The filter is the sampler's, not the texture's, so the same picture can
    // be read with the points of the texels where they are and with them
    // spread out.
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    // The depth of the pass: the map is a picture of what is nearest the
    // light, and "nearest" is a question about the depth.
    gl::GenRenderbuffers(1, &depthBuffer_);
    gl::BindRenderbuffer(GL_RENDERBUFFER, depthBuffer_);
    gl::RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, resolution, resolution);
    gl::BindRenderbuffer(GL_RENDERBUFFER, 0);

    gl::GenFramebuffers(1, &framebuffer_);
    gl::BindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                             depthBuffer_);
    // The colour of the layer is attached per pass, when the cascade is known.
    gl::FramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture_, 0, 0);

    const GLenum status = gl::CheckFramebufferStatus(GL_FRAMEBUFFER);
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        Log::error("Shadow: the map's framebuffer is not complete (0x%04X)", status);
        destroy();
        return false;
    }

    resolution_ = resolution;
    layers_ = layers;
    Log::info("Shadow: a %dx%d map with %d layer(s)", resolution, resolution, layers);
    return true;
}

bool ShadowMap::initialize(const ShadowSettings& settings) {
    setSettings(settings);
    return ready();
}

void ShadowMap::setSettings(const ShadowSettings& settings) {
    settings_ = settings;
    const int resolution = std::clamp(settings.resolution, 256, 8192);
    const int layers = std::clamp(settings.cascades, 1, 4);
    if (ready() && resolution == resolution_ && layers == layers_) return;
    create(resolution, layers);
}

void ShadowMap::fit(const std::vector<Vec3>& centres, const std::vector<float>& radii,
                    const Vec3& eye, const Vec3& forward, float nearPlane, float farPlane) {
    if (!ready()) return;

    // Where the stretches of the scene end, from the eye out.
    //
    // Half of them are spread evenly and half of them are spread
    // logarithmically: the even ones give the far ground the same share of the
    // scene, and the logarithmic ones give the near ground the texels it
    // needs - a shadow of a box at the camera's feet is a shadow of a few
    // pixels, and one at the horizon is a shadow of a smear.
    cascadeSplits_.clear();
    const float closest = std::fmax(nearPlane, 0.05f);
    for (int i = 1; i <= layers_; ++i) {
        const float t = (float)i / (float)layers_;
        const float even = nearPlane + (farPlane - nearPlane) * t;
        const float logarithmic = closest * std::pow(farPlane / closest, t);
        cascadeSplits_.push_back(std::fmin(farPlane, even * 0.5f + logarithmic * 0.5f));
    }

    lightMatrices_.assign((size_t)layers_, Mat4::identity());
    for (int cascade = 0; cascade < layers_; ++cascade)
        fitCascade(cascade, centres, radii, eye, forward, nearPlane, farPlane);
}

void ShadowMap::fitCascade(int cascade, const std::vector<Vec3>& centres,
                           const std::vector<float>& radii, const Vec3& eye,
                           const Vec3& forward, float nearPlane, float farPlane) {
    // Where this stretch of the scene begins and ends.
    const float from = cascade == 0 ? nearPlane : cascadeSplits_[(size_t)cascade - 1];
    const float to = cascadeSplits_[(size_t)cascade];

    // The things in it: the bodies that are within this stretch, measured from
    // the eye along the way the camera looks.
    Vec3 low{1e9f, 1e9f, 1e9f};
    Vec3 high{-1e9f, -1e9f, -1e9f};
    bool any = false;
    for (size_t i = 0; i < centres.size(); ++i) {
        const Vec3 offset = centres[i] - eye;
        const float depth = dot(offset, forward);
        if (depth < from || depth > to) continue;
        const float radius = radii[i];
        low.x = std::fmin(low.x, centres[i].x - radius);
        low.y = std::fmin(low.y, centres[i].y - radius);
        low.z = std::fmin(low.z, centres[i].z - radius);
        high.x = std::fmax(high.x, centres[i].x + radius);
        high.y = std::fmax(high.y, centres[i].y + radius);
        high.z = std::fmax(high.z, centres[i].z + radius);
        any = true;
    }

    if (!any) {
        // Nothing in this stretch: a small box around the eye, so that the
        // shader has a matrix and the map is not empty.
        low = eye - Vec3{kEmptyBox, kEmptyBox, kEmptyBox};
        high = eye + Vec3{kEmptyBox, kEmptyBox, kEmptyBox};
    }

    const Vec3 centre = (low + high) * 0.5f;
    const Vec3 extent = high - low;
    const float radius = extent.length() * 0.5f + kLightMargin;

    // The light's own box: the sun is far enough away that the light is
    // parallel, and the box is what it can see.
    const Vec3 direction = sunDirection(settings_);
    const Vec3 lightEye = centre - direction * (radius + kLightMargin);
    const Vec3 up = std::fabs(direction.y) > 0.99f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
    const Mat4 view = lookAt(lightEye, centre, up);

    // The corners of the box in the light's own space: the light looks down
    // its own -Z, so the depth of the box is measured backwards.
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (int corner = 0; corner < 8; ++corner) {
        const Vec3 point{(corner & 1) ? high.x : low.x, (corner & 2) ? high.y : low.y,
                         (corner & 4) ? high.z : low.z};
        const Vec3 inLight = view.transformPoint(point);
        minX = std::fmin(minX, inLight.x);
        maxX = std::fmax(maxX, inLight.x);
        minY = std::fmin(minY, inLight.y);
        maxY = std::fmax(maxY, inLight.y);
        minZ = std::fmin(minZ, inLight.z);
        maxZ = std::fmax(maxZ, inLight.z);
    }

    // A little more room than the box needs, so that the shadow of a thing
    // that is just outside the box is not cut in half.
    const float pad = 1.0f;
    const Mat4 projection = ortho(minX - pad, maxX + pad, minY - pad, maxY + pad, -maxZ,
                                  -minZ);
    lightMatrices_[(size_t)cascade] = projection * view;
}

void ShadowMap::beginPass(int cascade) {
    if (!ready() || cascade < 0 || cascade >= layers_) return;

    gl::BindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    gl::FramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture_, 0, cascade);
    glViewport(0, 0, resolution_, resolution_);

    // The colour of the map is the distance, and the distance of nothing is
    // as far as the map goes: a fragment that nothing is in front of is in
    // the light.
    const float furthest = 1.0f;
    glClearColor(furthest, furthest, furthest, 1.0f);
    glClearDepth(1.0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void ShadowMap::endPass() {
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace s3d
