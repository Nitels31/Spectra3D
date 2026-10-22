#include "render/WaterSystem.hpp"

#include "core/Log.hpp"
#include "platform/GlFunctions.hpp"

#include <algorithm>
#include <vector>

namespace s3d {
namespace {

// How many cells a side the grid of the water has.
//
// The waves are worked out per point, and the cell has to be small enough
// that a wave is made of a good many of them: a wave nine metres long over
// a sheet two hundred metres wide is eight cells of it, and eight is
// enough for the crest to be a crest and not a staircase.
//
// A hundred and sixty cells a side is twenty-five thousand points and fifty
// thousand triangles, and it is the same for every sheet of water: the grid
// is the grid of the world and the sheet is scaled to it.
constexpr int kGridCells = 160;

// The unit of the texture the picture of the scene is copied into.
constexpr int kSceneTextureUnit = 1;
// The unit the shadow map is read from. The third one and not the second:
// the second is the one the soft shadows are read with, and a single tap
// wants the texels where they are.
constexpr int kShadowTextureUnit = 3;

// The sky the water reflects.
//
// The engine's skybox is six ordinary textures, and the water would have to
// look up all six of them to find the one texel it wants - and the sky in
// the water is blurred by the waves anyway. What the water needs from the
// sky is its *colour*: the light of the horizon and the light of the
// zenith.
//
// They are worked out of the sun's own settings, so that the sky in the
// water is the sky of the picture: a low sun makes a low and warm horizon,
// and a high sun makes a pale one.
//
// And they are kept close to the sky of the picture on purpose. The sky in
// the water is the same sky as the one above the water, and a water that
// reflects a brighter sky than the picture has is a picture of two skies.
void skyColours(const ShadowSettings& settings, Vec3& horizon, Vec3& zenith) {
    const Vec3 sun = sunDirection(settings);
    // How near the horizon the sun is: one on the horizon and none at the
    // top of the sky.
    const float low = 1.0f - clampf(sun.y, 0.0f, 1.0f);

    zenith = settings.skyColour * 0.75f + Vec3{0.01f, 0.035f, 0.11f};
    horizon = settings.skyColour * 1.35f + settings.sunColour * (0.05f + 0.55f * low * low);
}

} // namespace

WaterSystem::~WaterSystem() { release(); }

void WaterSystem::release() {
    if (gridBuffer_) {
        if (gl::DeleteBuffers) gl::DeleteBuffers(1, &gridBuffer_);
        gridBuffer_ = 0;
    }
    if (indexBuffer_) {
        if (gl::DeleteBuffers) gl::DeleteBuffers(1, &indexBuffer_);
        indexBuffer_ = 0;
    }
    indexCount_ = 0;
    if (sceneTexture_) {
        glDeleteTextures(1, &sceneTexture_);
        sceneTexture_ = 0;
    }
    sceneWidth_ = 0;
    sceneHeight_ = 0;
    sceneValid_ = false;
}

void WaterSystem::initialize() {
    // The program. A program that does not load is not a reason to draw no
    // water: the log says what is missing and the engine goes on.
    program_.load(shaderPath("water/water.vert"), shaderPath("water/water.frag"));
    buildGrid();
}

void WaterSystem::buildGrid() {
    if (!gl::GenBuffers || !gl::BindBuffer || !gl::BufferData || !gl::VertexAttribPointer ||
        !gl::EnableVertexAttribArray || !gl::DisableVertexAttribArray) {
        Log::warn("Water: the driver has no buffers, and the water needs a grid to be drawn on");
        return;
    }

    // The points of the grid: a sheet that runs from -1 to 1 in both
    // directions, flat, with the origin in the middle of it. The sheet is
    // scaled to the body that is the water, and the waves are worked out
    // where it landed in the world.
    const int side = kGridCells + 1;
    std::vector<float> points;
    points.reserve((size_t)side * side * 3);
    for (int z = 0; z < side; ++z) {
        const float tz = -1.0f + 2.0f * (float)z / (float)kGridCells;
        for (int x = 0; x < side; ++x) {
            const float tx = -1.0f + 2.0f * (float)x / (float)kGridCells;
            points.push_back(tx);
            points.push_back(0.0f);
            points.push_back(tz);
        }
    }

    // The indices: two triangles to a cell. The grid is wound so that the
    // face of the water looks up, at the sky and not at the bottom.
    std::vector<unsigned int> indices;
    indices.reserve((size_t)kGridCells * kGridCells * 6);
    for (int z = 0; z < kGridCells; ++z) {
        for (int x = 0; x < kGridCells; ++x) {
            const unsigned int a = (unsigned int)(z * side + x);
            const unsigned int b = a + 1;
            const unsigned int c = a + (unsigned int)side;
            const unsigned int d = c + 1;
            indices.push_back(a);
            indices.push_back(c);
            indices.push_back(b);
            indices.push_back(b);
            indices.push_back(c);
            indices.push_back(d);
        }
    }

    if (gridBuffer_) gl::DeleteBuffers(1, &gridBuffer_);
    if (indexBuffer_) gl::DeleteBuffers(1, &indexBuffer_);

    gl::GenBuffers(1, &gridBuffer_);
    gl::BindBuffer(GL_ARRAY_BUFFER, gridBuffer_);
    gl::BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(points.size() * sizeof(float)), points.data(),
                   GL_STATIC_DRAW);

    gl::GenBuffers(1, &indexBuffer_);
    gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer_);
    gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(indices.size() * sizeof(unsigned int)),
                   indices.data(), GL_STATIC_DRAW);

    gl::BindBuffer(GL_ARRAY_BUFFER, 0);
    gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    indexCount_ = (int)indices.size();
}

bool WaterSystem::wanted(const PhysicsWorld& world) const {
    for (size_t i = 0; i < world.bodies.size(); ++i) {
        const RigidBody& body = world.bodies[i];
        if (body.visible && body.isWater()) return true;
    }
    return false;
}

bool WaterSystem::bendsLight(const PhysicsWorld& world) const {
    for (size_t i = 0; i < world.bodies.size(); ++i) {
        const RigidBody& body = world.bodies[i];
        if (body.visible && body.isWater() && body.water.refraction) return true;
    }
    return false;
}

bool WaterSystem::ensureSceneTexture(int width, int height) {
    if (sceneTexture_ && sceneWidth_ == width && sceneHeight_ == height) return true;
    if (sceneTexture_) {
        glDeleteTextures(1, &sceneTexture_);
        sceneTexture_ = 0;
    }
    sceneWidth_ = width;
    sceneHeight_ = height;
    // The copy is asked about again when the picture is made again: the
    // picture of the scene is a new picture then, and a driver that will not
    // copy out of the window's own buffer may have a new reason.
    sceneChecked_ = false;

    // The format of the window's own buffer, so that the copy into the
    // texture is a copy and not a conversion: a window with no alpha has
    // three channels, and a texture with four of them is read as if the
    // fourth one were full of ones.
    //
    // GL_RGBA8 and GL_RGB8 are the sized formats of OpenGL 3.0, and the copy
    // out of the window's own buffer is OpenGL 1.1: the whole of this is
    // inside the 3.3 the engine asks for.
    int alphaBits = 8;
    glGetIntegerv(GL_ALPHA_BITS, &alphaBits);

    glGenTextures(1, &sceneTexture_);
    glBindTexture(GL_TEXTURE_2D, sceneTexture_);
    const GLint internalFormat = alphaBits >= 8 ? GL_RGBA8 : GL_RGB8;
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0,
                 alphaBits >= 8 ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    // The picture of the scene is read with the filter on: the bend of the
    // light is a soft thing, and a bend with a stair in it is not a bend.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return sceneTexture_ != 0;
}

void WaterSystem::captureScene(int x, int y, int width, int height) {
    sceneValid_ = false;
    if (!sceneCopyWorks_) return;
    if (width < 1 || height < 1) return;
    if (!ensureSceneTexture(width, height)) return;

    // The picture of the scene, as it is on the screen.
    //
    // The copy is out of the back buffer of the window, and the window's
    // own buffer is the one the scene was just drawn into. The rectangle is
    // the scene's own: the picture of the scene is the scene, and the parts
    // of the window that are not the scene are not in it.
    glBindTexture(GL_TEXTURE_2D, sceneTexture_);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, x, y, width, height);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Whether the copy worked. It is asked once - the first time, and again
    // whenever the picture is made again - because a driver that will not
    // copy out of the window's own buffer will not copy out of it the
    // second time either, and asking every frame would cost a flush of the
    // whole command stream every frame.
    if (!sceneChecked_) {
        sceneChecked_ = true;
        if (glGetError() != GL_NO_ERROR) {
            sceneCopyWorks_ = false;
            Log::warn("Water: this driver will not copy the picture of the scene, and the "
                      "water will not bend what is under it");
            return;
        }
    }
    sceneValid_ = true;
}

void WaterSystem::draw(const PhysicsWorld& world, const Camera& camera, const Mat4& view,
                       const Mat4& projection, const FogSettings& fog, const ShadowMap& shadows,
                       int viewportX, int viewportY, int viewportWidth, int viewportHeight,
                       float time, int hiddenBody) {
    if (!program_.ready() || !gridBuffer_ || !indexBuffer_) return;
    if (viewportWidth < 1 || viewportHeight < 1) return;

    program_.use();

    // THE SHARED THINGS
    //
    // The camera and the two matrices are the ones the scene was drawn with,
    // and the sun and the sky and the fog and the shadows are the ones of
    // the picture: the water is in the picture, and it has to be in the same
    // picture.
    program_.setMat4("uView", view.m);
    program_.setMat4("uProjection", projection.m);
    program_.setVec3("uCameraPosition", camera.position.x, camera.position.y, camera.position.z);
    program_.setFloat("uTime", time);

    const ShadowSettings& settings = shadows.settings();
    const Vec3 sun = sunDirection(settings);
    program_.setVec3("uSunDirection", sun.x, sun.y, sun.z);
    program_.setVec3("uSunColour", settings.sunColour.x, settings.sunColour.y,
                      settings.sunColour.z);
    program_.setVec3("uSkyColour", settings.skyColour.x, settings.skyColour.y,
                      settings.skyColour.z);

    Vec3 horizon, zenith;
    skyColours(settings, horizon, zenith);
    program_.setVec3("uSkyHorizon", horizon.x, horizon.y, horizon.z);
    program_.setVec3("uSkyZenith", zenith.x, zenith.y, zenith.z);

    // The fog: the same air as everything else in the picture and the same
    // numbers.
    program_.setVec3("uFogColour", fog.colour.x, fog.colour.y, fog.colour.z);
    program_.setFloat("uFogDensity", fog.density);
    program_.setFloat("uFogFalloff", fog.falloff);
    program_.setFloat("uFogHeight", fog.height);
    program_.setFloat("uFogScatter", fog.scatter);
    program_.setFloat("uFogForward", fog.forward);

    // The shadow map: the same picture the world is drawn with.
    const int quality = settings.quality == ShadowQuality::Off     ? 0
                        : settings.quality == ShadowQuality::Hard   ? 1
                        : settings.quality == ShadowQuality::Filtered ? 2
                                                                     : 3;
    program_.setInt("uShadowEnabled", quality == 0 ? 0 : 1);
    program_.setFloat("uShadowBias", settings.bias);
    program_.setFloat("uShadowNormalTexels", settings.normalOffset);
    program_.setInt("uCascadeCount", std::max(1, shadows.layers()));
    program_.setVec3("uSceneCentre", shadows.sceneCentre().x, shadows.sceneCentre().y,
                     shadows.sceneCentre().z);

    float matrices[16 * 4] = {};
    const std::vector<Mat4>& lights = shadows.lightMatrices();
    for (int i = 0; i < 4; ++i) {
        const Mat4& light = i < (int)lights.size() ? lights[(size_t)i] : Mat4::identity();
        std::copy(light.m, light.m + 16, matrices + i * 16);
    }
    program_.setMat4Array("uLightViewProjection[0]", matrices, 4);

    float params[4 * 4] = {};
    const std::vector<float>& radii = shadows.cascadeRadii();
    const std::vector<float>& ranges = shadows.cascadeDepthRanges();
    const std::vector<float>& texels = shadows.cascadeTexelSizes();
    for (int i = 0; i < 4; ++i) {
        params[i * 4 + 0] = i < (int)radii.size() ? radii[(size_t)i] : 0.0f;
        params[i * 4 + 1] = i < (int)ranges.size() ? ranges[(size_t)i] : 1.0f;
        params[i * 4 + 2] = i < (int)texels.size() ? texels[(size_t)i] : 1.0f;
        params[i * 4 + 3] = 0.0f;
    }
    program_.setVec4Array("uCascadeParams[0]", params, 4);

    gl::ActiveTexture(GL_TEXTURE0 + kShadowTextureUnit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, shadows.texture());
    program_.setInt("uShadowMap", kShadowTextureUnit);

    // The picture of the scene, for the bend of the light.
    gl::ActiveTexture(GL_TEXTURE0 + kSceneTextureUnit);
    glBindTexture(GL_TEXTURE_2D, sceneTexture_);
    program_.setInt("uSceneColour", kSceneTextureUnit);

    // Where the picture of the scene is in the window: the water has to know
    // where it is on the screen to know what it is bending.
    program_.setVec4("uViewport", (float)viewportX, (float)viewportY, (float)viewportWidth,
                     (float)viewportHeight);

    // THE BLEND
    //
    // The water is drawn with a premultiplied alpha: the colour is written
    // ready to be added to the picture, and the bottom comes through what is
    // left of the transparency. It is the only way the reflection survives -
    // the sky in the water is light that comes *off* the surface, and the
    // surface does not swallow it, but a mixed alpha multiplies the
    // reflection by the transparency as well.
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    // The water does not write into the depth: a sheet of water that stops
    // what is behind it from being drawn is not a sheet of water.
    glDepthMask(GL_FALSE);

    // THE WATER ITSELF
    for (size_t index = 0; index < world.bodies.size(); ++index) {
        const RigidBody& body = world.bodies[index];
        if (!body.visible || !body.isWater()) continue;
        if ((int)index == hiddenBody) continue;

        const WaterSettings& water = body.water;

        program_.setVec3("uShallowColour", water.shallowColour.x, water.shallowColour.y,
                         water.shallowColour.z);
        program_.setVec3("uDeepColour", water.deepColour.x, water.deepColour.y,
                         water.deepColour.z);
        program_.setInt("uDepthColour", water.depthColour ? 1 : 0);
        program_.setFloat("uDepthFade", water.depthFade);
        program_.setFloat("uWaterDepth", water.depth);
        program_.setFloat("uClarity", water.clarity);
        program_.setFloat("uOpacity", water.opacity);
        program_.setFloat("uShoreFade", water.shoreFade);

        program_.setFloat("uReflectivity", water.reflectivity);
        program_.setFloat("uFresnelPower", water.fresnelPower);
        program_.setFloat("uFresnelIntensity", water.fresnelIntensity);
        program_.setFloat("uSpecular", water.specular);
        program_.setFloat("uShininess", water.shininess);

        program_.setFloat("uFoam", water.foam);
        program_.setVec3("uFoamColour", water.foamColour.x, water.foamColour.y,
                         water.foamColour.z);

        program_.setFloat("uDetailStrength", water.detailStrength);
        program_.setFloat("uDetailScale", water.detailScale);
        program_.setFloat("uDetailSpeed", water.detailSpeed);

        program_.setInt("uRefraction", (water.refraction && sceneValid_) ? 1 : 0);
        program_.setFloat("uRefractionStrength", water.refractionStrength);

        program_.setFloat("uDarkening", water.darkening);

        program_.setFloat("uWaveHeight", water.waveHeight);
        program_.setFloat("uWaveLength", water.waveLength);
        program_.setFloat("uWaveSpeed", water.waveSpeed);
        program_.setFloat("uWaveSteepness", water.waveSteepness);
        program_.setVec2("uWindDirection", water.windDirection.x, water.windDirection.y);

        // The sheet in the world: where it stands and how big it is.
        //
        // The waves are worked out in the world, so the sheet is level
        // whatever the body is turned to: a water surface that is tilted is
        // a water surface that is falling over.
        const Mat4 model = Mat4::translation(body.position) *
                           Mat4::scale(Vec3{body.halfExtents.x, 1.0f, body.halfExtents.z});
        program_.setMat4("uModel", model.m);

        gl::BindBuffer(GL_ARRAY_BUFFER, gridBuffer_);
        gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (const void*)0);
        gl::EnableVertexAttribArray(0);
        gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer_);
        glDrawElements(GL_TRIANGLES, (GLsizei)indexCount_, GL_UNSIGNED_INT, (const void*)0);
        gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        gl::BindBuffer(GL_ARRAY_BUFFER, 0);
        gl::DisableVertexAttribArray(0);
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    gl::ActiveTexture(GL_TEXTURE0);
    gl::UseProgram(0);
}

} // namespace s3d
