#include "render/SceneRenderer.hpp"

#include "core/Log.hpp"
#include "core/Math.hpp"
#include "platform/GlFunctions.hpp"

#include <algorithm>

namespace s3d {
namespace {


// The two halves of the grid. The minor lines are one unit apart and are the
// ones the eye reads as a floor; the major lines are ten units apart and they
// are what carries the grid out to the horizon, where one unit is smaller
// than a pixel and would only alias.
constexpr float kMinorStep = 1.0f;
constexpr float kMinorExtent = 60.0f;
constexpr float kMinorSegment = 6.0f;
constexpr float kMajorStep = 10.0f;
constexpr float kMajorExtent = 400.0f;
constexpr float kMajorSegment = 40.0f;
constexpr float kAxisExtent = 100.0f;

// Where the grid fades out. The minor lines are gone before the major ones
// take over, so the two hands over at the same distance and there is no ring
// where the fine grid stops and the coarse one has not started.
//
// The far end of the major fade is where the grid meets the horizon: from a
// camera six units up, 360 units of ground is one degree below the eye line,
// which is a line the eye cannot pick out of the sky's own horizon.
constexpr float kMinorFadeStart = 28.0f;
constexpr float kMinorFadeEnd = 60.0f;
constexpr float kMajorFadeStart = 55.0f;
constexpr float kMajorFadeEnd = 360.0f;

// The grid against the sky. The sky's ground is a light grey, so the lines are
// dark and they are drawn with enough transparency to read as lines and not as
// a wash over the ground.
constexpr float kMinorAlpha = 0.38f;
constexpr float kMajorAlpha = 0.62f;
constexpr float kAxisAlpha = 0.85f;

// Viewport background. Anything the grid and the bodies do not cover ends up in
// this colour.
constexpr float kSkyRed = 0.18f, kSkyGreen = 0.22f, kSkyBlue = 0.28f;

// Fallback colors for bodies without an explicit material color.
const Color kBodyPalette[8] = {{0.86f, 0.36f, 0.33f}, {0.36f, 0.62f, 0.90f},
                               {0.95f, 0.76f, 0.31f}, {0.46f, 0.79f, 0.45f},
                               {0.72f, 0.49f, 0.86f}, {0.91f, 0.53f, 0.26f},
                               {0.35f, 0.75f, 0.73f}, {0.83f, 0.83f, 0.87f}};

// The four corners of a camera's far plane, in the camera's own space.
//
// The frustum is what says "this is a camera" - a box on its own is a box. The
// shape is the field of view, widened sideways because a screen is wider than
// it is tall; the icon is a schematic, not a promise about the exact view.
void cameraFrustum(const RigidBody& body, float length, Vec3 out[4]) {
    const float halfHeight = length * std::tan(body.cameraFov * 0.5f * kPi / 180.0f);
    const float halfWidth = halfHeight * 1.6f;
    out[0] = Vec3{halfWidth, halfHeight, -length};
    out[1] = Vec3{-halfWidth, halfHeight, -length};
    out[2] = Vec3{-halfWidth, -halfHeight, -length};
    out[3] = Vec3{halfWidth, -halfHeight, -length};
}

// How far the frustum reaches, and how far in front of the body the lens sits.
constexpr float kCameraFrustumLength = 1.2f;
constexpr float kCameraLensOffset = 0.12f;

// 1 at the near end of the range, 0 at the far end, smooth in between.
float fadeOut(float distance, float start, float end) {
    if (distance <= start) return 1.0f;
    if (distance >= end) return 0.0f;
    const float t = (distance - start) / (end - start);
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

} // namespace

void SceneRenderer::initialize(const Vec3& arenaMin, const Vec3& arenaMax) {
    glShadeModel(GL_SMOOTH);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_NORMALIZE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    const GLfloat ambient[] = {0.24f, 0.24f, 0.27f, 1.0f};
    const GLfloat diffuse[] = {0.95f, 0.94f, 0.90f, 1.0f};
    const GLfloat position[] = {0.35f, 0.85f, 0.40f, 0.0f};
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
    glLightfv(GL_LIGHT0, GL_POSITION, position);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    // MODULATE, not REPLACE: the lit colour multiplies the texture, so a texture
    // still darkens and tints with the light instead of glowing flat. Set once
    // here rather than per draw call - it is part of the texture state that
    // drawImportedMesh toggles GL_TEXTURE_2D around.
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    // A white texel, for the things that have no picture of their own.
    glGenTextures(1, &whiteTexture_);
    glBindTexture(GL_TEXTURE_2D, whiteTexture_);
    const unsigned char white[4] = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    // The programs. The scene's own, and the one that fills the shadow map.
    //
    // A program that does not load is not a reason to stop: the engine draws
    // the scene with what it has, and the log says what is missing.
    sceneProgram_.load(shaderPath("scene.vert"), shaderPath("scene.frag"));
    shadowProgram_.load(shaderPath("shadows/shadow_depth.vert"),
                        shaderPath("shadows/shadow_depth.frag"));
    shadowDebugProgram_.load(shaderPath("shadows/shadow_debug.vert"),
                             shaderPath("shadows/shadow_debug.frag"));
    shadowMap_.initialize(shadowMap_.settings());

    primitives_.build(arenaMin, arenaMax);
    buildArenaGrid();
}

void SceneRenderer::setShadowSettings(const ShadowSettings& settings) {
    shadowMap_.setSettings(settings);
}

void cameraFrustum(const Camera& camera, int width, int height, double& halfWidth,
                    double& halfHeight) {
    // The frustum is the camera's field of view: the vertical one over the
    // height, and the horizontal one following from the aspect.
    //
    // It is a free function and not a line inside the renderer because the
    // editor picks with the same numbers, and the two have to be the same
    // numbers: a frustum that is wider than the camera's own would make the
    // pointer and the picture disagree about where a thing is, and the horizon
    // would sit at the wrong height.
    const double tanHalf = std::tan(camera.fovDeg * kPi / 360.0);
    const double h = (double)std::max(1, height);
    const double w = (double)std::max(1, width);
    halfHeight = tanHalf * kNearPlane;
    halfWidth = tanHalf * kNearPlane * (w / h);
}

void SceneRenderer::setProjection(const Camera& camera, const Viewport& viewport) {
    double halfWidth = 0.0, halfHeight = 0.0;
    cameraFrustum(camera, viewport.width, viewport.height, halfWidth, halfHeight);

    projection_[0] = (float)(2.0 * kNearPlane / (2.0 * halfWidth));
    projection_[5] = (float)(2.0 * kNearPlane / (2.0 * halfHeight));
    projection_[8] = 0.0f;
    projection_[9] = 0.0f;
    projection_[10] = (float)(-(kFarPlane + kNearPlane) / (kFarPlane - kNearPlane));
    projection_[11] = -1.0f;
    projection_[14] = (float)(-2.0 * kFarPlane * kNearPlane / (kFarPlane - kNearPlane));
    projection_[1] = projection_[2] = projection_[3] = projection_[4] = 0.0f;
    projection_[6] = projection_[7] = projection_[12] = projection_[13] = projection_[15] = 0.0f;

    // The same numbers as a matrix, for the shaders. The two have to be the
    // same numbers: the fixed pipeline draws the grid and the gizmo, and the
    // shader draws the bodies, and they are all in the same picture.
    projectionMatrix_ = frustum((float)-halfWidth, (float)halfWidth, (float)-halfHeight,
                                 (float)halfHeight, (float)kNearPlane, (float)kFarPlane);
    std::copy(projectionMatrix_.m, projectionMatrix_.m + 16, projection_);

    glViewport(viewport.glX, viewport.glY, viewport.width, viewport.height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-halfWidth, halfWidth, -halfHeight, halfHeight, kNearPlane, kFarPlane);

    // The viewport of the scene, kept for the shadow pass: the map is a
    // picture of its own and the scene's viewport would cut it to a corner of
    // the screen.
    viewportX_ = viewport.glX;
    viewportY_ = viewport.glY;
    viewportWidth_ = viewport.width;
    viewportHeight_ = viewport.height;
}

void SceneRenderer::setViewMatrix(const Camera& camera) {
    Vec3 forward, right, up;
    camera.basis(forward, right, up);

    const float m[16] = {
        right.x, up.x, -forward.x, 0.0f,
        right.y, up.y, -forward.y, 0.0f,
        right.z, up.z, -forward.z, 0.0f,
        -dot(right, camera.position), -dot(up, camera.position),
        dot(forward, camera.position), 1.0f};

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glMultMatrixf(m);
    std::copy(m, m + 16, view_);

    // The same matrix, for the shaders.
    viewMatrix_ = Mat4::identity();
    viewMatrix_.m[0] = right.x;  viewMatrix_.m[4] = right.y;  viewMatrix_.m[8] = right.z;
    viewMatrix_.m[1] = up.x;     viewMatrix_.m[5] = up.y;     viewMatrix_.m[9] = up.z;
    viewMatrix_.m[2] = -forward.x; viewMatrix_.m[6] = -forward.y; viewMatrix_.m[10] = -forward.z;
    viewMatrix_.m[12] = -dot(right, camera.position);
    viewMatrix_.m[13] = -dot(up, camera.position);
    viewMatrix_.m[14] = dot(forward, camera.position);
}

void SceneRenderer::beginFrame(int framebufferWidth, int framebufferHeight) {
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(kSkyRed, kSkyGreen, kSkyBlue, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void SceneRenderer::renderWorld(const PhysicsWorld& world, const MeshRegistry& meshes,
                                 const TextureRegistry& textures, const Camera& camera,
                                 const Viewport& viewport, bool showGrid, bool showVectors,
                                 float alpha, int hiddenBody) {
    if (viewport.width < 1 || viewport.height < 1) return;

    glEnable(GL_SCISSOR_TEST);
    glScissor(viewport.glX, viewport.glY, viewport.width, viewport.height);
    setProjection(camera, viewport);

    setViewMatrix(camera);
    glDisable(GL_LIGHTING);
    extractFrustum(projection_, view_);

    // The shadow of the sun, before anything is drawn: the shadow of a thing
    // is in the picture of the ground it falls on, and the ground cannot be
    // drawn before the map is full.
    //
    // And only when the map has gone stale: while the editor sits still, the
    // world and the eye are the same, and the map would be the same picture
    // again and again.
    if (shadowsEnabled() && shadowProgram_.ready()) {
        if (shadowMapIsStale(world, camera)) {
            renderShadowMap(world, meshes, textures, camera, alpha, hiddenBody);
            rememberShadowState(world, camera);
        }
    }

    // The sky first, with the depth test off: it is behind everything, and
    // everything else is drawn over it.
    sky_.draw(camera);

    if (showGrid) drawArena(camera);
    drawBodies(world, meshes, textures, alpha, hiddenBody);

    if (showVectors) drawDebugVectors(world);
    glEnable(GL_LIGHTING);
    glDisable(GL_SCISSOR_TEST);

    // And the map, if it is asked for: what the sun sees, in the corner.
    if (showShadowDebug_) renderShadowDebug(camera, viewport);
}

// The map on the screen: what the sun sees.
//
// It is drawn over the scene, in the corner of the viewport, so that the map
// and the picture it belongs to can be looked at together.
void SceneRenderer::renderShadowDebug(const Camera& camera, const Viewport& viewport) {
    if (!shadowDebugProgram_.ready() || !shadowMap_.ready()) return;
    if (viewport.width < 8 || viewport.height < 8) return;

    // A square in the corner, a tenth of the viewport's width.
    const int size = std::max(64, std::min(viewport.width, viewport.height) / 4);
    const int x = viewport.glX + viewport.width - size - 8;
    const int y = viewport.glY + viewport.height - size - 8;

    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, size, size);
    glViewport(x, y, size, size);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_BLEND);

    shadowDebugProgram_.use();
    gl::ActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D_ARRAY, shadowMap_.texture());
    shadowDebugProgram_.setInt("uShadowMap", 4);
    // The layer of the stretch the eye is in: the map of a stretch nobody is
    // looking at is a map of nothing.
    int layer = 0;
    {
        Vec3 forward, right, up;
        camera.basis(forward, right, up);
        const float depth = dot(camera.position - camera.position, forward);
        const std::vector<float>& ends = shadowMap_.cascadeSplits();
        for (size_t i = 0; i < ends.size(); ++i)
            if (depth <= ends[i]) {
                layer = (int)i;
                break;
            }
        layer = std::max(0, std::min(layer, shadowMap_.layers() - 1));
    }
    shadowDebugProgram_.setInt("uLayer", layer);
    shadowDebugProgram_.setFloat("uFarPlane", 1.0f);
    shadowDebugProgram_.setFloat("uTexelSize", 1.0f / (float)std::max(1, shadowMap_.resolution()));
    gl::ActiveTexture(GL_TEXTURE0);

    // The quad: two triangles over the whole of the little square.
    static const float kQuad[12] = {-1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f,
                                    -1.0f, -1.0f, 1.0f,  1.0f, -1.0f, 1.0f};
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, kQuad);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableClientState(GL_VERTEX_ARRAY);

    gl::UseProgram(0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);

    // And the scene's own picture back.
    glViewport(viewport.glX, viewport.glY, viewport.width, viewport.height);
    glScissor(viewport.glX, viewport.glY, viewport.width, viewport.height);
    glDisable(GL_SCISSOR_TEST);
}

// The shadow map is drawn again only when the world or the eye has moved.
//
// The map is a picture of the world as the sun sees it, and the sun does not
// move and the world does not move while the editor sits still: the map would
// be the same picture a thousand times a second. So the state the map depends
// on is remembered, and the pass is skipped when it has not changed.
//
// The cost of the test is a walk over the bodies that cast, and the cost of
// the pass is a walk over the bodies that cast plus four draws of the scene.
// The test is the cheaper of the two by a long way.
bool SceneRenderer::shadowMapIsStale(const PhysicsWorld& world, const Camera& camera) const {
    const ShadowState& state = shadowState_;
    if (!state.ready) return true;
    if (state.resolution != shadowMap_.resolution() || state.cascades != shadowMap_.layers())
        return true;
    if (state.eye.x != camera.position.x || state.eye.y != camera.position.y ||
        state.eye.z != camera.position.z || state.yaw != camera.yaw ||
        state.pitch != camera.pitch || state.fov != camera.fovDeg)
        return true;

    // The bodies that cast: where they are and how big they are.
    std::vector<std::array<float, 7>> casters;
    casters.reserve(world.bodies.size());
    for (size_t i = 0; i < world.bodies.size(); ++i) {
        const RigidBody& body = world.bodies[i];
        if (!body.visible || !body.castsShadow) continue;
        casters.push_back({body.position.x, body.position.y, body.position.z,
                           body.boundRadius, body.meshScale, (float)body.mesh,
                           body.shape == Shape::Mesh ? 1.0f : 0.0f});
    }
    if (casters.size() != state.casters.size()) return true;
    for (size_t i = 0; i < casters.size(); ++i)
        for (size_t k = 0; k < casters[i].size(); ++k)
            if (casters[i][k] != state.casters[i][k]) return true;
    return false;
}

void SceneRenderer::rememberShadowState(const PhysicsWorld& world, const Camera& camera) {
    ShadowState& state = shadowState_;
    state.casters.clear();
    state.casters.reserve(world.bodies.size());
    for (size_t i = 0; i < world.bodies.size(); ++i) {
        const RigidBody& body = world.bodies[i];
        if (!body.visible || !body.castsShadow) continue;
        state.casters.push_back({body.position.x, body.position.y, body.position.z,
                                 body.boundRadius, body.meshScale, (float)body.mesh,
                                 body.shape == Shape::Mesh ? 1.0f : 0.0f});
    }
    state.eye = camera.position;
    state.yaw = camera.yaw;
    state.pitch = camera.pitch;
    state.fov = camera.fovDeg;
    state.resolution = shadowMap_.resolution();
    state.cascades = shadowMap_.layers();
    state.ready = true;
}

// The shadow pass: the map of what the sun hit first.
//
// The bodies are drawn twice - once into the map and once into the scene -
// and the map is the cheaper of the two: a position and nothing else.
void SceneRenderer::renderShadowMap(const PhysicsWorld& world, const MeshRegistry& meshes,
                                    const TextureRegistry& textures, const Camera& camera,
                                    float alpha, int hiddenBody) {
    // Where the map is fitted: the slice of the view that each stretch of the
    // scene covers. The bodies are not what decides it - a ground of forty
    // metres and a box of one metre in the same map means that the box's
    // shadow is a smear of fifty texels.
    Vec3 forward, right, up;
    camera.basis(forward, right, up);
    const float halfHeight = std::tan(camera.fovDeg * 0.5f * kPi / 180.0f);
    const float aspect = (float)std::max(1, viewportWidth_) / (float)std::max(1, viewportHeight_);
    shadowMap_.fit(camera.position, forward, right, up, halfHeight, aspect, (float)kNearPlane,
                    (float)kFarPlane);

    // The map is a picture of its own: the viewport of the scene and the
    // scissor around it would cut it to a corner of the screen.
    glDisable(GL_SCISSOR_TEST);

    shadowProgram_.use();
    for (int cascade = 0; cascade < shadowMap_.layers(); ++cascade) {
        shadowMap_.beginPass(cascade);
        const Mat4& light = shadowMap_.lightMatrices()[(size_t)cascade];
        shadowProgram_.setMat4("uLightViewProjection", light.m);
        shadowProgram_.setMat4("uModel", Mat4::identity().m);

        for (int index = 0; index < (int)world.bodies.size(); ++index) {
            const RigidBody& body = world.bodies[(size_t)index];
            if (!body.visible || index == hiddenBody) continue;
            if (!body.castsShadow) continue;

            const Vec3 drawPosition{lerpf(body.prevPosition.x, body.position.x, alpha),
                                    lerpf(body.prevPosition.y, body.position.y, alpha),
                                    lerpf(body.prevPosition.z, body.position.z, alpha)};
            const Quat drawOrientation = nlerp(body.prevOrientation, body.orientation, alpha);

            if (body.hasMesh()) {
                const MeshAsset* mesh = meshes.at(body.mesh);
                if (!mesh) continue;
                const float scale = body.meshScale * mesh->scale;
                Mat4 model = Mat4::compose(drawPosition, drawOrientation,
                                           Vec3{scale, scale, scale}) *
                             Mat4::translation(-mesh->center);
                shadowProgram_.setMat4("uModel", model.m);
                for (const MeshPart& part : mesh->parts) {
                    if (part.indices.empty()) continue;
                    glEnableClientState(GL_VERTEX_ARRAY);
                    glVertexPointer(3, GL_FLOAT, (GLsizei)(sizeof(float) * kMeshVertexFloats),
                                    part.vertices.data() + kMeshPositionOffset);
                    glDrawElements(GL_TRIANGLES, (GLsizei)part.indices.size(), GL_UNSIGNED_INT,
                                   part.indices.data());
                    glDisableClientState(GL_VERTEX_ARRAY);
                }
                continue;
            }

            // The primitives: the same shapes, the same transforms, and only
            // the position matters for a shadow.
            Mat4 model = Mat4::compose(drawPosition, drawOrientation, Vec3{1.0f, 1.0f, 1.0f});
            switch (body.shape) {
            case Shape::Sphere: {
                const float r = body.halfExtents.x;
                model = Mat4::compose(drawPosition, drawOrientation, Vec3{r, r, r});
                shadowProgram_.setMat4("uModel", model.m);
                glCallList(primitives_.sphere());
                break;
            }
            case Shape::Cylinder: {
                model = Mat4::compose(drawPosition, drawOrientation,
                                      Vec3{body.cylinderRadius, body.halfExtents.y,
                                           body.cylinderRadius});
                shadowProgram_.setMat4("uModel", model.m);
                glCallList(primitives_.cylinder());
                break;
            }
            case Shape::Capsule: {
                const float r = body.capsuleRadius;
                const float halfHeight = body.capsuleHeight * 0.5f;
                model = Mat4::compose(drawPosition, drawOrientation, Vec3{r, r, r}) *
                        Mat4::translation(Vec3{0.0f, halfHeight, 0.0f});
                shadowProgram_.setMat4("uModel", model.m);
                glCallList(primitives_.sphere());
                model = Mat4::compose(drawPosition, drawOrientation, Vec3{r, r, r}) *
                        Mat4::translation(Vec3{0.0f, -halfHeight, 0.0f});
                shadowProgram_.setMat4("uModel", model.m);
                glCallList(primitives_.sphere());
                model = Mat4::compose(drawPosition, drawOrientation,
                                      Vec3{r, body.halfExtents.y, r}) *
                        Mat4::translation(Vec3{0.0f, -halfHeight, 0.0f});
                shadowProgram_.setMat4("uModel", model.m);
                glCallList(primitives_.cylinder());
                break;
            }
            case Shape::Cone: {
                model = Mat4::compose(drawPosition, drawOrientation,
                                      Vec3{body.coneRadius, body.coneHeight * 0.5f,
                                           body.coneRadius});
                shadowProgram_.setMat4("uModel", model.m);
                glCallList(primitives_.cone());
                break;
            }
            case Shape::Pyramid:
            case Shape::Prism: {
                const float radius = body.shape == Shape::Pyramid ? body.pyramidBaseRadius
                                                                 : body.prismRadius;
                const float height = body.shape == Shape::Pyramid ? body.pyramidHeight
                                                                 : body.prismHeight;
                model = Mat4::compose(drawPosition, drawOrientation,
                                      Vec3{radius, height * 0.5f, radius});
                shadowProgram_.setMat4("uModel", model.m);
                glCallList(primitives_.pyramid());
                break;
            }
            case Shape::Camera:
            case Shape::Box:
            default: {
                model = Mat4::compose(drawPosition, drawOrientation, body.halfExtents);
                shadowProgram_.setMat4("uModel", model.m);
                glCallList(primitives_.cube());
                break;
            }
            }
        }
        shadowMap_.endPass();
    }
    gl::UseProgram(0);

    // And the scene's own picture back.
    glViewport(viewportX_, viewportY_, viewportWidth_, viewportHeight_);
    glEnable(GL_SCISSOR_TEST);
}

void SceneRenderer::extractFrustum(const float* projection, const float* view) {
    // clipSpace = projection * view, computed on the CPU so culling never needs
    // a synchronous glGet* round trip to the driver.
    float clip[16];
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
                sum += projection[k * 4 + row] * view[col * 4 + k];
            clip[col * 4 + row] = sum;
        }

    static const int axisIndex[6][2] = {{3, 0}, {3, 0}, {3, 1},
                                        {3, 1}, {3, 2}, {3, 2}};
    static const float sign[6] = {1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f};
    for (int i = 0; i < 6; ++i) {
        const int a = axisIndex[i][0], b = axisIndex[i][1];
        float plane[4];
        for (int k = 0; k < 4; ++k) plane[k] = clip[k * 4 + a] + sign[i] * clip[k * 4 + b];
        const float len = std::sqrt(plane[0] * plane[0] + plane[1] * plane[1] +
                                    plane[2] * plane[2]);
        if (len <= 1e-8f) continue;
        for (int k = 0; k < 4; ++k) frustum_[i][k] = plane[k] / len;
    }
}

bool SceneRenderer::sphereVisible(const Vec3& center, float radius) const {
    for (int i = 0; i < 6; ++i)
        if (frustum_[i][0] * center.x + frustum_[i][1] * center.y +
                frustum_[i][2] * center.z + frustum_[i][3] <
            -radius)
            return false;
    return true;
}

void SceneRenderer::buildArenaGrid() {
    arenaGridReady_ = true;
    arenaPositions_.clear();
    arenaColors_.clear();
    arenaVertexColors_.clear();
    arenaVertexAlpha_.clear();
    arenaPositions_.reserve((size_t)ArenaGrid::kVertexCount * 3);
    arenaColors_.reserve((size_t)ArenaGrid::kVertexCount * 4);
    arenaVertexColors_.reserve((size_t)ArenaGrid::kVertexCount * 4);
    arenaVertexAlpha_.reserve((size_t)ArenaGrid::kVertexCount);

    // One vertex: the position in the patch's own frame, the colour, and how
    // transparent the vertex is before the distance fade is applied.
    auto emit = [&](float x, float z, float r, float g, float b, float alpha) {
        arenaPositions_.push_back(x);
        arenaPositions_.push_back(0.0f);
        arenaPositions_.push_back(z);
        arenaVertexColors_.push_back(r);
        arenaVertexColors_.push_back(g);
        arenaVertexColors_.push_back(b);
        arenaVertexColors_.push_back(0.0f);
        arenaVertexAlpha_.push_back(alpha);
    };

    // Dark: the grid is drawn over the sky's own ground, which is a light
    // grey, and a line that is only a shade darker than the ground is a line
    // nobody can see.
    constexpr float kMinorR = 0.09f, kMinorG = 0.12f, kMinorB = 0.11f;
    constexpr float kMajorR = 0.16f, kMajorG = 0.20f, kMajorB = 0.18f;

    // A patch of lines around the origin, each cut into short segments. The
    // segments are what let a line fade along its length: a line drawn as one
    // long segment has two far ends, and a line whose two ends are both
    // transparent is a line nobody can see.
    auto patch = [&](int steps, float step, float extent, float segment, int segments,
                     float r, float g, float b, float alpha) {
        for (int i = 0; i < steps; ++i) {
            const float v = -extent + (float)i * step;
            for (int direction = 0; direction < 2; ++direction) {
                for (int s = 0; s < segments; ++s) {
                    const float from = -extent + (float)s * segment;
                    const float to = from + segment;
                    if (direction == 0) {
                        emit(v, from, r, g, b, alpha);
                        emit(v, to, r, g, b, alpha);
                    } else {
                        emit(from, v, r, g, b, alpha);
                        emit(to, v, r, g, b, alpha);
                    }
                }
            }
        }
    };

    patch(ArenaGrid::kMinorSteps, kMinorStep, kMinorExtent, kMinorSegment,
          ArenaGrid::kMinorSegments, kMinorR, kMinorG, kMinorB, kMinorAlpha);
    patch(ArenaGrid::kMajorSteps, kMajorStep, kMajorExtent, kMajorSegment,
          ArenaGrid::kMajorSegments, kMajorR, kMajorG, kMajorB, kMajorAlpha);

    // World axes: X red, Y green, Z blue. They are at the world origin and they
    // stay there - the grid follows the camera, the axes do not.
    emit(-kAxisExtent, 0.0f, 0.75f, 0.25f, 0.20f, kAxisAlpha);
    emit(kAxisExtent, 0.0f, 0.75f, 0.25f, 0.20f, kAxisAlpha);
    emit(0.0f, -kAxisExtent, 0.20f, 0.45f, 0.75f, kAxisAlpha);
    emit(0.0f, kAxisExtent, 0.20f, 0.45f, 0.75f, kAxisAlpha);
    emit(0.0f, 0.0f, 0.35f, 0.85f, 0.40f, kAxisAlpha);
    emit(0.0f, 0.0f, 0.35f, 0.85f, 0.40f, kAxisAlpha);

    arenaColors_.assign(arenaVertexColors_.size(), 0.0f);
}

void SceneRenderer::drawArena(const Camera& camera) {
    if (!arenaGridReady_) buildArenaGrid();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glLineWidth(1.0f);

    // The grid is a window on the world grid that follows the camera: the
    // patch is snapped to the major step and moved with the camera, so every
    // line lands back on the world position it was on before and there is no
    // edge of the grid to walk to.
    const float offsetX = std::floor(camera.position.x / kMajorStep) * kMajorStep;
    const float offsetZ = std::floor(camera.position.z / kMajorStep) * kMajorStep;

    // The transparency of every vertex, from its distance to the camera. A
    // line that fades along its length is what makes the grid look endless:
    // the far half of every line dissolves into the horizon.
    const size_t vertexCount = arenaVertexAlpha_.size();
    const size_t minorEnd = (size_t)ArenaGrid::kMinorVertices;
    const size_t majorEnd = (size_t)ArenaGrid::kGridVertices;

    const float* positions = arenaPositions_.data();
    const float* vertexColors = arenaVertexColors_.data();
    const float* vertexAlpha = arenaVertexAlpha_.data();
    float* colors = arenaColors_.data();
    for (size_t i = 0; i < vertexCount; ++i, positions += 3, vertexColors += 4,
                      vertexAlpha += 1) {
        // The grid is in the patch's frame and is moved with the camera; the
        // axes are already in the world and stay where they are.
        const bool axis = i >= majorEnd;
        const float wx = axis ? positions[0] : positions[0] + offsetX;
        const float wz = axis ? positions[2] : positions[2] + offsetZ;
        const float dx = wx - camera.position.x;
        const float dz = wz - camera.position.z;
        const float distance = std::sqrt(dx * dx + dz * dz);

        float fade;
        if (i < minorEnd) {
            fade = fadeOut(distance, kMinorFadeStart, kMinorFadeEnd);
        } else if (i < majorEnd) {
            fade = fadeOut(distance, kMajorFadeStart, kMajorFadeEnd);
        } else {
            // The axes stand at the world origin, so it is the origin's
            // distance that counts for them - and the Y axis is vertical, so
            // it is the origin itself.
            const float adx = (i < majorEnd + 2) ? dx : (0.0f - camera.position.x);
            const float adz = (i < majorEnd + 4) ? dz : (0.0f - camera.position.z);
            fade = fadeOut(std::sqrt(adx * adx + adz * adz), kMajorFadeStart, kMajorFadeEnd);
        }

        colors[0] = vertexColors[0];
        colors[1] = vertexColors[1];
        colors[2] = vertexColors[2];
        colors[3] = vertexAlpha[0] * fade;
        colors += 4;
    }

    // One draw call for the whole grid. Lighting is disabled here, so the
    // per-vertex colour array is used directly, exactly as glColor4f +
    // glVertex3f immediate mode used it - and the vertex/segment order is
    // unchanged, which matters because the grid is alpha blended.
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 12, arenaPositions_.data());
    glColorPointer(4, GL_FLOAT, 16, arenaColors_.data());

    // The grid, moved with the camera.
    glPushMatrix();
    glTranslatef(offsetX, 0.0f, offsetZ);
    glDrawArrays(GL_LINES, 0, (GLsizei)ArenaGrid::kGridVertices);
    glPopMatrix();

    // The axes stay at the world origin.
    glDrawArrays(GL_LINES, (GLsizei)ArenaGrid::kGridVertices, (GLsizei)ArenaGrid::kAxisVertices);

    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void SceneRenderer::drawDebugVectors(const PhysicsWorld& world) {
    glLineWidth(2.0f);
    lines::arrow(Vec3{0.0f, 0.0f, 0.0f}, world.gravity.normalized() * 6.0f,
                 Vec3{1.0f, 0.30f, 0.25f});
    glLineWidth(1.0f);
}


void SceneRenderer::drawBodies(const PhysicsWorld& world, const MeshRegistry& meshes,
                               const TextureRegistry& textures, float alpha, int hiddenBody) {
    beginScenePass();

    int paletteIndex = 0;
    for (int index = 0; index < (int)world.bodies.size(); ++index) {
        const RigidBody& body = world.bodies[(size_t)index];
        if (!body.visible) continue;
        // The camera the viewport is looking through sits at the eye. Drawn
        // there, its own icon is a hand's width from the near plane and fills
        // the screen - the view becomes a grey wall. It is not drawn, the way
        // an engine does not draw the camera it is rendering from.
        if (index == hiddenBody) continue;
        if (!sphereVisible(body.position, body.boundRadius)) continue;

        // Interpolated between the state before the last physics step and the
        // state after it. This is the whole point of the fixed-step accumulator:
        // physics advances in 1/60 s jumps, and without this a 59.8 Hz display
        // shows one frozen frame and one double-speed frame every five seconds.
        // nlerp rather than slerp - the angle between two consecutive 1/60 s
        // steps is small enough that normalising the lerp is indistinguishable.
        const Vec3 drawPosition{
            lerpf(body.prevPosition.x, body.position.x, alpha),
            lerpf(body.prevPosition.y, body.position.y, alpha),
            lerpf(body.prevPosition.z, body.position.z, alpha)};
        const Quat drawOrientation = nlerp(body.prevOrientation, body.orientation, alpha);

        const bool hasExplicitColor = body.material.color.x != 0.0f ||
                                      body.material.color.y != 0.0f ||
                                      body.material.color.z != 0.0f;
        const Color color = hasExplicitColor
                                ? Color{body.material.color.x, body.material.color.y,
                                        body.material.color.z}
                                : kBodyPalette[paletteIndex & 7];

        // A mesh is drawn with the scene's program; a primitive is drawn with
        // the same program, and the same matrix, so the two look like the
        // same world.
        const MeshAsset* mesh = body.hasMesh() ? meshes.at(body.mesh) : nullptr;
        if (mesh) {
            const float scale = body.meshScale * mesh->scale;
            const Mat4 model = Mat4::compose(drawPosition, drawOrientation,
                                              Vec3{scale, scale, scale}) *
                               Mat4::translation(-mesh->center);
            drawMeshWithShader(*mesh, textures, model, Color{1.0f, 1.0f, 1.0f});
            ++paletteIndex;
            continue;
        }

        // The primitives. The colour of the body is the colour of the thing:
        // the primitives have no colours of their own, and the tint is what
        // the editor picked.
        sceneProgram_.setVec3("uTint", color.r, color.g, color.b);
        sceneProgram_.setInt("uUseVertexColour", 0);

        Mat4 model = Mat4::compose(drawPosition, drawOrientation, Vec3{1.0f, 1.0f, 1.0f});
        switch (body.shape) {
        case Shape::Sphere: {
            const float r = body.halfExtents.x;
            model = Mat4::compose(drawPosition, drawOrientation, Vec3{r, r, r});
            sceneProgram_.setMat4("uModel", model.m);
            glCallList(primitives_.sphere());
            break;
        }
        case Shape::Cylinder: {
            model = Mat4::compose(drawPosition, drawOrientation,
                                  Vec3{body.cylinderRadius, body.halfExtents.y,
                                       body.cylinderRadius});
            sceneProgram_.setMat4("uModel", model.m);
            glCallList(primitives_.cylinder());
            break;
        }
        case Shape::Capsule: {
            const float r = body.capsuleRadius;
            const float halfHeight = body.capsuleHeight * 0.5f;
            model = Mat4::compose(drawPosition, drawOrientation, Vec3{r, r, r}) *
                    Mat4::translation(Vec3{0.0f, halfHeight, 0.0f});
            sceneProgram_.setMat4("uModel", model.m);
            glCallList(primitives_.sphere());
            model = Mat4::compose(drawPosition, drawOrientation, Vec3{r, r, r}) *
                    Mat4::translation(Vec3{0.0f, -halfHeight, 0.0f});
            sceneProgram_.setMat4("uModel", model.m);
            glCallList(primitives_.sphere());
            model = Mat4::compose(drawPosition, drawOrientation,
                                  Vec3{r, body.halfExtents.y, r}) *
                    Mat4::translation(Vec3{0.0f, -halfHeight, 0.0f});
            sceneProgram_.setMat4("uModel", model.m);
            glCallList(primitives_.cylinder());
            break;
        }
        case Shape::Cone: {
            model = Mat4::compose(drawPosition, drawOrientation,
                                  Vec3{body.coneRadius, body.coneHeight * 0.5f,
                                       body.coneRadius});
            sceneProgram_.setMat4("uModel", model.m);
            glCallList(primitives_.cone());
            break;
        }
        case Shape::Pyramid:
        case Shape::Prism: {
            const float radius =
                body.shape == Shape::Pyramid ? body.pyramidBaseRadius : body.prismRadius;
            const float height =
                body.shape == Shape::Pyramid ? body.pyramidHeight : body.prismHeight;
            model = Mat4::compose(drawPosition, drawOrientation,
                                  Vec3{radius, height * 0.5f, radius});
            sceneProgram_.setMat4("uModel", model.m);
            glCallList(primitives_.pyramid());
            break;
        }
        case Shape::Camera: {
            // A camera is drawn as what it is: a body with a lens, and the
            // frustum it looks through. The frustum is the part that says
            // something - a box on its own is a box.
            model = Mat4::compose(drawPosition, drawOrientation, body.halfExtents);
            sceneProgram_.setMat4("uModel", model.m);
            glCallList(primitives_.cube());
            drawCameraEntity(body, model);
            break;
        }
        default: {
            model = Mat4::compose(drawPosition, drawOrientation, body.halfExtents);
            sceneProgram_.setMat4("uModel", model.m);
            glCallList(primitives_.cube());
            break;
        }
        }
        ++paletteIndex;
    }
    gl::UseProgram(0);

    // The fixed pipeline's own matrix, back to the camera's: the lines and the
    // gizmo that are drawn after the bodies are in the world, and the matrix
    // of the last body is not the world.
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view_);
}

// The uniforms the scene pass shares: the sun, the sky and the shadow map.
void SceneRenderer::beginScenePass() {
    if (!sceneProgram_.ready()) return;
    sceneProgram_.use();

    const ShadowSettings& settings = shadowMap_.settings();
    const Vec3 sun = sunDirection(settings);
    sceneProgram_.setVec3("uSunDirection", sun.x, sun.y, sun.z);
    sceneProgram_.setVec3("uSunColour", settings.sunColour.x, settings.sunColour.y,
                          settings.sunColour.z);
    sceneProgram_.setVec3("uSkyColour", settings.skyColour.x, settings.skyColour.y,
                          settings.skyColour.z);
    sceneProgram_.setVec3("uGroundColour", settings.groundColour.x, settings.groundColour.y,
                          settings.groundColour.z);
    sceneProgram_.setFloat("uAmbient", settings.ambient);
    sceneProgram_.setFloat("uSpecular", settings.specular);
    sceneProgram_.setFloat("uShininess", settings.shininess);
    sceneProgram_.setVec3("uFogColour", settings.fogColour.x, settings.fogColour.y,
                          settings.fogColour.z);
    sceneProgram_.setFloat("uFogDensity", settings.fogDensity);
    sceneProgram_.setMat4("uView", viewMatrix_.m);
    sceneProgram_.setMat4("uProjection", projectionMatrix_.m);

    // The shadow map: the same picture read two ways. The filter belongs to
    // the sampler and not to the texture, so the one map is both.
    const int quality = settings.quality == ShadowQuality::Off     ? 0
                        : settings.quality == ShadowQuality::Hard   ? 1
                        : settings.quality == ShadowQuality::Filtered ? 2
                                                                     : 3;
    sceneProgram_.setInt("uShadowMode", quality);
    sceneProgram_.setFloat("uShadowTexelSize", 1.0f / (float)std::max(1, shadowMap_.resolution()));
    sceneProgram_.setFloat("uShadowBias", settings.bias);
    sceneProgram_.setFloat("uShadowRadius", settings.radius);
    sceneProgram_.setInt("uCascadeCount", shadowMap_.layers());

    gl::ActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D_ARRAY, shadowMap_.texture());
    sceneProgram_.setInt("uShadowMap", 2);
    gl::ActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D_ARRAY, shadowMap_.texture());
    sceneProgram_.setInt("uShadowMapFiltered", 3);
    // And the white texel on the first unit: the primitives have no picture
    // of their own, and a sampler with nothing bound to it reads black - a
    // whole object drawn in the colour of nothing.
    gl::ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, whiteTexture_);
    sceneProgram_.setInt("uAlbedoMap", 0);

    // The matrices of the cascades and where the stretches of the scene end.
    float matrices[16 * 4] = {};
    const std::vector<Mat4>& lights = shadowMap_.lightMatrices();
    for (int i = 0; i < 4; ++i) {
        const Mat4& light = i < (int)lights.size() ? lights[(size_t)i] : Mat4::identity();
        std::copy(light.m, light.m + 16, matrices + i * 16);
    }
    sceneProgram_.setMat4Array("uLightViewProjection[0]", matrices, 4);

    float splits[4] = {};
    const std::vector<float>& ends = shadowMap_.cascadeSplits();
    for (int i = 0; i < 4; ++i) splits[i] = i < (int)ends.size() ? ends[(size_t)i] : 0.0f;
    sceneProgram_.setFloatArray("uCascadeSplits[0]", splits, 4);
}

// One mesh, with the scene's program.
bool SceneRenderer::drawMeshWithShader(const MeshAsset& mesh, const TextureRegistry& textures,
                                       const Mat4& model, const Color& tint) {
    if (!sceneProgram_.ready()) return false;

    const size_t stride = sizeof(float) * kMeshVertexFloats;
    sceneProgram_.setMat4("uModel", model.m);
    sceneProgram_.setVec3("uTint", tint.r, tint.g, tint.b);

    for (const MeshPart& part : mesh.parts) {
        // A mesh with colours of its own - the ground has them, and they are
        // the colours of the biomes - and a mesh without, which is every
        // imported model.
        const bool vertexColors =
            part.colors.size() == part.vertices.size() / kMeshVertexFloats * 3 &&
            !part.colors.empty();
        sceneProgram_.setInt("uUseVertexColour", vertexColors ? 1 : 0);

        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_NORMAL_ARRAY);
        if (vertexColors) {
            glEnableClientState(GL_COLOR_ARRAY);
            glColorPointer(3, GL_FLOAT, 0, part.colors.data());
        }
        glVertexPointer(3, GL_FLOAT, (GLsizei)stride, part.vertices.data() + kMeshPositionOffset);
        glNormalPointer(GL_FLOAT, (GLsizei)stride, part.vertices.data() + kMeshNormalOffset);

        // The picture on the surface, or the white one: a mesh without a
        // picture is not a mesh without a colour.
        const unsigned tex = textures.at(part.texture);
        const bool hasTexture = tex != 0;
        if (hasTexture) {
            gl::ActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, tex);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glTexCoordPointer(2, GL_FLOAT, (GLsizei)stride,
                              part.vertices.data() + kMeshTexCoordOffset);
            sceneProgram_.setInt("uAlbedoMap", 0);
        } else {
            gl::ActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, whiteTexture_);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            sceneProgram_.setInt("uAlbedoMap", 0);
        }

        if (!part.indices.empty()) {
            glDrawElements(GL_TRIANGLES, (GLsizei)part.indices.size(), GL_UNSIGNED_INT,
                           part.indices.data());
        } else {
            glDrawArrays(GL_TRIANGLES, 0,
                         (GLsizei)(part.vertices.size() / kMeshVertexFloats));
        }

        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        if (vertexColors) glDisableClientState(GL_COLOR_ARRAY);
    }
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    return true;
}

void SceneRenderer::drawCameraEntity(const RigidBody& body, const Mat4& model) {
    // The lens: a wireframe frustum from the front of the body to the far
    // plane. Immediate mode rather than a display list, because its size
    // depends on the field of view, which is a per-camera value.
    //
    // The lines are drawn by the fixed pipeline and the frustum is in the
    // camera's own space, so the matrix has to be the body's: without it the
    // frustum is drawn at the origin of the world.
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(model.m);
    glDisable(GL_LIGHTING);

    // Not named `far`: windows.h defines it as a macro, and an array called
    // that does not survive the preprocessor.
    Vec3 corners[4];
    cameraFrustum(body, kCameraFrustumLength, corners);

    // The camera the game looks through is drawn in a different colour. There
    // is no other way to tell, in a scene with a few cameras in it, which one
    // of them the game is going to use.
    if (body.activeCamera)
        glColor3f(1.0f, 0.82f, 0.30f);
    else
        glColor3f(0.45f, 0.85f, 1.0f);
    glLineWidth(1.5f);
    glBegin(GL_LINES);
    for (int i = 0; i < 4; ++i) {
        glVertex3f(0.0f, 0.0f, -kCameraLensOffset);
        glVertex3f(corners[i].x, corners[i].y, corners[i].z);
        glVertex3f(corners[i].x, corners[i].y, corners[i].z);
        glVertex3f(corners[(i + 1) & 3].x, corners[(i + 1) & 3].y, corners[(i + 1) & 3].z);
    }
    glEnd();
    glLineWidth(1.0f);
}

void SceneRenderer::renderSelection(const RigidBody& body) {
    glDisable(GL_LIGHTING);
    glLineWidth(2.0f);
    glColor3f(1.0f, 0.85f, 0.25f);

    if (body.shape == Shape::Camera) {
        // A camera is outlined by its frustum, not by a box around its body:
        // the box would be a dozen pixels across, and what a camera is is the
        // part of the world it looks at.
        Vec3 corners[4];
        cameraFrustum(body, kCameraFrustumLength, corners);
        // The frustum is in the camera's own space, so it is turned into world
        // space before it is drawn - the same way the icon is.
        const Vec3 lens =
            body.position + rotate(body.orientation, Vec3{0.0f, 0.0f, -kCameraLensOffset});
        glBegin(GL_LINES);
        for (int i = 0; i < 4; ++i) {
            const Vec3 corner = body.position + rotate(body.orientation, corners[i]);
            glVertex3f(lens.x, lens.y, lens.z);
            glVertex3f(corner.x, corner.y, corner.z);
        }
        glEnd();
    } else if (body.shape == Shape::Sphere) {
        const float r = body.halfExtents.x + 0.06f;
        for (int plane = 0; plane < 3; ++plane) {
            glBegin(GL_LINE_LOOP);
            for (int i = 0; i < 28; ++i) {
                const float angle = 2.0f * kPi * (float)i / 28.0f;
                const float ca = std::cos(angle) * r, sa = std::sin(angle) * r;
                Vec3 p = body.position;
                if (plane == 0) { p.x += ca; p.y += sa; }
                if (plane == 1) { p.x += ca; p.z += sa; }
                if (plane == 2) { p.z += ca; p.y += sa; }
                glVertex3f(p.x, p.y, p.z);
            }
            glEnd();
        }
    } else {
        const Vec3& he = body.halfExtents;
        Vec3 corners[8];
        for (int k = 0; k < 8; ++k)
            corners[k] = body.position +
                         rotate(body.orientation,
                                Vec3{(k & 1) ? he.x : -he.x, (k & 2) ? he.y : -he.y,
                                     (k & 4) ? he.z : -he.z});
        glBegin(GL_LINES);
        for (int k = 0; k < 8; ++k)
            for (int bit = 0; bit < 3; ++bit) {
                const int j = k ^ (1 << bit);
                if (j <= k) continue;
                glVertex3f(corners[k].x, corners[k].y, corners[k].z);
                glVertex3f(corners[j].x, corners[j].y, corners[j].z);
            }
        glEnd();
    }

    glLineWidth(1.0f);
    glEnable(GL_LIGHTING);
}

void SceneRenderer::renderGizmo(const Vec3& position, const Vec3* axes, float length,
                               float ringRadius, const Vec3* ringU, const Vec3* ringV,
                               int activeAxis, GizmoShape shape) {
    static const Color kAxisColors[3] = {{1.0f, 0.30f, 0.30f}, {0.35f, 1.0f, 0.35f},
                                         {0.40f, 0.60f, 1.0f}};
    // The rotate ring is sampled the same number of times the picker samples it,
    // so the curve on screen is the curve that is grabbable.
    constexpr int kRingSteps = 48;

    glDisable(GL_LIGHTING);
    glDepthFunc(GL_ALWAYS); // gizmo stays visible through geometry

    if (shape == GizmoShape::Rings) {
        // The ring basis comes from the caller, which builds it with the same
        // gizmo::ringFrame() the picker uses. Recomputing it here is how the drawn
        // ring and the grabbable ring drift apart.
        for (int i = 0; i < 3; ++i) {
            const float dim = activeAxis == i ? 1.0f : 0.62f;
            const Color color = kAxisColors[i].scaled(dim);
            glLineWidth(activeAxis == i ? 4.0f : 2.0f);
            glColor3f(color.r, color.g, color.b);
            glBegin(GL_LINE_LOOP);
            for (int step = 0; step < kRingSteps; ++step) {
                const float a = (float)step * (2.0f * kPi / (float)kRingSteps);
                const Vec3 point =
                    position + (ringU[i] * std::cos(a) + ringV[i] * std::sin(a)) * ringRadius;
                glVertex3f(point.x, point.y, point.z);
            }
            glEnd();
        }
        glDepthFunc(GL_LEQUAL);
        glLineWidth(1.0f);
        glEnable(GL_LIGHTING);
        return;
    }

    for (int i = 0; i < 3; ++i) {
        const Vec3& axis = axes[i];
        Vec3 u = std::fabs(axis.y) < 0.9f ? cross(axis, Vec3{0.0f, 1.0f, 0.0f})
                                          : cross(axis, Vec3{1.0f, 0.0f, 0.0f});
        u = u.normalized();
        const Vec3 v = cross(axis, u);
        const Vec3 tip = position + axis * length;

        const float dim = activeAxis == i ? 1.0f : 0.72f;
        const Color color = kAxisColors[i].scaled(dim);
        glLineWidth(activeAxis == i ? 4.0f : 2.0f);
        glColor3f(color.r, color.g, color.b);
        glBegin(GL_LINES);
        glVertex3f(position.x, position.y, position.z);
        glVertex3f(tip.x, tip.y, tip.z);
        glEnd();

        if (shape == GizmoShape::Cubes) {
            // Cube handle for the scale mode.
            const float h = 0.05f * length + 0.035f;
            Vec3 c[8];
            int n = 0;
            for (int sa = -1; sa <= 1; sa += 2)
                for (int sb = -1; sb <= 1; sb += 2)
                    for (int sc = -1; sc <= 1; sc += 2)
                        c[n++] = tip + axis * (sa * h) + u * (sb * h) + v * (sc * h);
            glBegin(GL_QUADS);
            glVertex3fv(&c[0].x); glVertex3fv(&c[1].x); glVertex3fv(&c[3].x); glVertex3fv(&c[2].x);
            glVertex3fv(&c[4].x); glVertex3fv(&c[6].x); glVertex3fv(&c[7].x); glVertex3fv(&c[5].x);
            glVertex3fv(&c[0].x); glVertex3fv(&c[2].x); glVertex3fv(&c[6].x); glVertex3fv(&c[4].x);
            glVertex3fv(&c[1].x); glVertex3fv(&c[5].x); glVertex3fv(&c[7].x); glVertex3fv(&c[3].x);
            glVertex3fv(&c[0].x); glVertex3fv(&c[4].x); glVertex3fv(&c[5].x); glVertex3fv(&c[1].x);
            glVertex3fv(&c[2].x); glVertex3fv(&c[3].x); glVertex3fv(&c[7].x); glVertex3fv(&c[6].x);
            glEnd();
        } else {
            // Cone handle for the move mode.
            const float tipLength = 0.13f * length + 0.05f;
            const float baseRadius = 0.055f * length + 0.03f;
            const Vec3 apex = tip + axis * tipLength;
            const Vec3 b0 = u * baseRadius, b1 = v * baseRadius;
            glBegin(GL_TRIANGLES);
            for (int k = 0; k < 4; ++k) {
                const float a0 = k * kPi * 0.5f, a1 = (k + 1) * kPi * 0.5f;
                const Vec3 p1 = tip + b0 * std::cos(a0) + b1 * std::sin(a0);
                const Vec3 p2 = tip + b0 * std::cos(a1) + b1 * std::sin(a1);
                glVertex3f(p1.x, p1.y, p1.z);
                glVertex3f(p2.x, p2.y, p2.z);
                glVertex3f(apex.x, apex.y, apex.z);
            }
            glEnd();
        }
    }
    glDepthFunc(GL_LEQUAL);
    glLineWidth(1.0f);
    glEnable(GL_LIGHTING);
}

} // namespace s3d
