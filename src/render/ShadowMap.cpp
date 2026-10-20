#include "render/ShadowMap.hpp"

#include "core/Log.hpp"
#include "platform/GlFunctions.hpp"

#include <algorithm>
#include <cmath>

namespace s3d {
namespace {

// How far the light stands outside the stretch of the scene it lights.
//
// It has to be enough that nothing is behind it - a light inside the scene
// has no shadows at all - and that the near plane of the box is in front of
// everything the stretch holds.
constexpr float kLightMargin = 4.0f;

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
    if (pointSampler_) gl::DeleteSamplers(1, &pointSampler_);
    if (linearSampler_) gl::DeleteSamplers(1, &linearSampler_);
    framebuffer_ = 0;
    depthBuffer_ = 0;
    texture_ = 0;
    pointSampler_ = 0;
    linearSampler_ = 0;
    resolution_ = 0;
    layers_ = 0;
    // The map is gone, and what is left of it is a picture of nothing.
    ++generation_;
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
    // The texture's own filter is the plain one. The two readings the shader
    // wants - the texels where they are, and the texels spread out - are two
    // samplers below, and a sampler overrides what the texture says.
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    // The two readings. The points of the texels where they are, for the one
    // tap that has to be exactly where it says, and the texels spread out, for
    // the filter that wants the average of the four around it.
    gl::GenSamplers(1, &pointSampler_);
    gl::SamplerParameteri(pointSampler_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl::SamplerParameteri(pointSampler_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl::SamplerParameteri(pointSampler_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::SamplerParameteri(pointSampler_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    gl::GenSamplers(1, &linearSampler_);
    gl::SamplerParameteri(linearSampler_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl::SamplerParameteri(linearSampler_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl::SamplerParameteri(linearSampler_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::SamplerParameteri(linearSampler_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

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

    // The map is a big thing: a 4096 map with four stretches is a hundred and
    // thirty megabytes of picture. With the shadows off it is neither drawn
    // nor read, so it is not kept either - and the machine that cannot afford
    // it is exactly the machine that wanted the shadows off.
    if (settings.quality == ShadowQuality::Off) {
        if (ready()) destroy();
        return;
    }

    if (ready() && resolution == resolution_ && layers == layers_) return;
    create(resolution, layers);
}

void ShadowMap::fit(const Vec3& centre, float radius) {
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
    cascadeDepthRanges_.assign((size_t)layers_, 1.0f);
    cascadeTexelSizes_.assign((size_t)layers_, 1.0f);
    for (int cascade = 0; cascade < layers_; ++cascade)
        fitCascade(cascade, centre, cascadeRadii_[(size_t)cascade]);
}

void ShadowMap::fitCascade(int cascade, const Vec3& centre, float radius) {
    // The sphere of the scene this stretch covers, and the box it makes in
    // the light's own space.
    const Vec3 direction = sunDirection(settings_);
    const Vec3 upAxis =
        std::fabs(direction.y) > 0.99f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
    // The light stands outside the stretch, so that nothing in it is behind
    // the light.
    const float margin = std::fmax(kLightMargin, radius * 0.25f);
    const Vec3 lightEye = centre + direction * (radius + margin);
    const Mat4 view = lookAt(lightEye, centre, upAxis);

    // The box is the tightest one that holds the sphere, and not the box of
    // the cube around it.
    //
    // The stretch covers a sphere of the radius, and a sphere seen from the
    // light is a disc of the same radius: the box is twice the radius across
    // and twice the radius deep. The corners of the cube of the same radius
    // reach further out - up to a half further, when the sun is in the
    // corner - and every texel spent on that extra is a texel the shadow does
    // not get. It is the difference between a sharp shadow and a soft one.
    const float half = radius + 1.0f;
    // The far end of the box: the far end of the sphere and a margin for the
    // things that stick out of it. The near end is the light itself, so that
    // nothing between the light and the scene is cut away.
    const float farPlane = 2.0f * radius + 2.0f * margin;

    const Mat4 projection = ortho(-half, half, -half, half, 0.0f, farPlane);
    lightMatrices_[(size_t)cascade] = projection * view;

    // The two numbers the shader needs to make sense of the map: how deep the
    // box is, and how big one of its texels is. Both are in metres, and both
    // are different in every stretch.
    cascadeDepthRanges_[(size_t)cascade] = farPlane;
    cascadeTexelSizes_[(size_t)cascade] = (2.0f * half) / (float)std::max(1, resolution_);
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
    // Both faces: a thing that is open - a plane, a sheet - has to be able to
    // cast a shadow, and a closed thing is the same from either side.
    glDisable(GL_CULL_FACE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void ShadowMap::endPass() {
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool ShadowMap::readLayer(int cascade, std::vector<unsigned short>& out) const {
    if (!ready() || cascade < 0 || cascade >= layers_) return false;
    const size_t count = (size_t)resolution_ * (size_t)resolution_;
    out.assign(count, 0);

    gl::BindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    gl::FramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture_, 0, cascade);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    // One channel of sixteen bits: the map holds a distance and nothing else,
    // and the read asks for exactly that.
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, resolution_, resolution_, GL_RED, GL_UNSIGNED_SHORT, out.data());
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

} // namespace s3d
