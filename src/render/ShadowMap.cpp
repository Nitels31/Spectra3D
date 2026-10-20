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

void ShadowMap::fit(const Vec3& centre, float radius) {
    if (!ready()) return;

    sceneCentre_ = centre;

    // The stretches of the scene: the first is the middle of it and the rest
    // are the halves, so that what is in the middle gets the texels and what
    // is at the edge does not steal them.
    cascadeRadii_.clear();
    const float outer = std::fmax(radius, 0.5f);
    for (int i = 0; i < layers_; ++i) {
        // The first layer is the smallest: the scene divided by two, once for
        // every layer above it.
        float r = outer;
        for (int k = 0; k < layers_ - 1 - i; ++k) r *= 0.5f;
        cascadeRadii_.push_back(r);
    }
    // And the splits are the same numbers, in the order the shader reads them:
    // the smallest first.
    cascadeSplits_.assign(cascadeRadii_.begin(), cascadeRadii_.end());

    lightMatrices_.assign((size_t)layers_, Mat4::identity());
    for (int cascade = 0; cascade < layers_; ++cascade)
        fitCascade(cascade, centre, cascadeRadii_[(size_t)cascade]);
}

void ShadowMap::fitCascade(int cascade, const Vec3& centre, float radius) {
    // The sphere of the scene this stretch covers, and the box it makes in
    // the light's own space.
    const Vec3 direction = sunDirection(settings_);
    const Vec3 upAxis =
        std::fabs(direction.y) > 0.99f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
    const Vec3 lightEye = centre + direction * (radius + kLightMargin);
    const Mat4 view = lookAt(lightEye, centre, upAxis);

    // The corners of the box in the light's own space: the light looks down
    // its own -Z, so the depth of the box is measured backwards.
    float lMinX = 1e9f, lMaxX = -1e9f, lMinY = 1e9f, lMaxY = -1e9f, lMinZ = 1e9f, lMaxZ = -1e9f;
    for (int corner = 0; corner < 8; ++corner) {
        const Vec3 point{(corner & 1) ? radius : -radius, (corner & 2) ? radius : -radius,
                         (corner & 4) ? radius : -radius};
        const Vec3 inLight = view.transformPoint(centre + point);
        lMinX = std::fmin(lMinX, inLight.x);
        lMaxX = std::fmax(lMaxX, inLight.x);
        lMinY = std::fmin(lMinY, inLight.y);
        lMaxY = std::fmax(lMaxY, inLight.y);
        lMinZ = std::fmin(lMinZ, inLight.z);
        lMaxZ = std::fmax(lMaxZ, inLight.z);
    }

    // A little more room than the box needs, so that the shadow of a thing
    // that is just outside the box is not cut in half.
    const float pad = 1.0f;
    const Mat4 projection = ortho(lMinX - pad, lMaxX + pad, lMinY - pad, lMaxY + pad, -lMaxZ,
                                  -lMinZ);
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
