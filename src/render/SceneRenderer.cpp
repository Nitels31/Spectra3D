#include "render/SceneRenderer.hpp"

#include "core/Math.hpp"

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

// Writes the quaternion rotation straight into the column-major 4x4 GL layout.
// This is the transpose of Mat3::fromQuat() with the same expressions in the
// same order, so it produces bit-identical floats without materialising and
// transposing an intermediate Mat3 (which also default-initialised 9 floats it
// immediately overwrote).
void applyRotation(const Quat& q) {
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    const float r0 = 1 - 2 * (yy + zz), r1 = 2 * (xy - wz), r2 = 2 * (xz + wy);
    const float r3 = 2 * (xy + wz), r4 = 1 - 2 * (xx + zz), r5 = 2 * (yz - wx);
    const float r6 = 2 * (xz - wy), r7 = 2 * (yz + wx), r8 = 1 - 2 * (xx + yy);

    const float m[16] = {r0, r3, r6, 0.0f, r1, r4, r7, 0.0f,
                         r2, r5, r8, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    glMultMatrixf(m);
}

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

    primitives_.build(arenaMin, arenaMax);
    buildArenaGrid();
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

    glViewport(viewport.glX, viewport.glY, viewport.width, viewport.height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-halfWidth, halfWidth, -halfHeight, halfHeight, kNearPlane, kFarPlane);
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

    // The sky first, with the depth test off: it is behind everything, and
    // everything else is drawn over it.
    sky_.draw(camera);

    if (showGrid) drawArena(camera);
    drawBodies(world, meshes, textures, alpha, hiddenBody);

    if (showVectors) drawDebugVectors(world);
    glEnable(GL_LIGHTING);
    glDisable(GL_SCISSOR_TEST);
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

// Draws an imported mesh inside the body transform the caller already installed.
// Returns false when the body has no usable mesh, so the caller can fall back to
// its primitive.
bool SceneRenderer::drawImportedMesh(const RigidBody& body, const MeshRegistry& meshes,
                                      const TextureRegistry& textures, const Quat& orientation) {
    if (!body.hasMesh()) return false;
    const MeshAsset* mesh = meshes.at(body.mesh);
    if (!mesh) return false;

    const float scale = body.meshScale * mesh->scale;
    applyRotation(orientation);
    glScalef(scale, scale, scale);
    glTranslatef(-mesh->center.x, -mesh->center.y, -mesh->center.z);

    const size_t stride = sizeof(float) * kMeshVertexFloats;

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    for (const MeshPart& part : mesh->parts) {
        glColor3f(part.color[0], part.color[1], part.color[2]);
        glVertexPointer(3, GL_FLOAT, (GLsizei)stride, part.vertices.data() + kMeshPositionOffset);
        glNormalPointer(GL_FLOAT, (GLsizei)stride,
                        part.vertices.data() + kMeshNormalOffset);

        // Texturing is per part, so a model with one mapped material and one flat
        // one draws both in the same loop. The flat part has texturing switched
        // off rather than bound to nothing: a texture name that was never given
        // an image is an incomplete texture, and an incomplete texture samples as
        // black on some drivers and white on others. Off is the answer that is the
        // same everywhere.
        const unsigned tex = textures.at(part.texture);
        const bool hasTexture = tex != 0;
        if (hasTexture) {
            glBindTexture(GL_TEXTURE_2D, tex);
            glEnable(GL_TEXTURE_2D);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glTexCoordPointer(2, GL_FLOAT, (GLsizei)stride,
                              part.vertices.data() + kMeshTexCoordOffset);
        } else {
            glDisable(GL_TEXTURE_2D);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        }

        if (!part.indices.empty()) {
            glDrawElements(GL_TRIANGLES, (GLsizei)part.indices.size(), GL_UNSIGNED_INT,
                           part.indices.data());
        } else {
            glDrawArrays(GL_TRIANGLES, 0,
                         (GLsizei)(part.vertices.size() / kMeshVertexFloats));
        }

        if (hasTexture) {
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glDisable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    return true;
}

void SceneRenderer::drawBodies(const PhysicsWorld& world, const MeshRegistry& meshes,
                               const TextureRegistry& textures, float alpha, int hiddenBody) {
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
        glColor3f(color.r, color.g, color.b);

        // Exactly one push/pop per body, whichever path is taken below.
        glPushMatrix();
        glTranslatef(drawPosition.x, drawPosition.y, drawPosition.z);
        if (!drawImportedMesh(body, meshes, textures, drawOrientation)) {
            switch (body.shape) {
            case Shape::Sphere: {
                const float r = body.halfExtents.x;
                glScalef(r, r, r);
                glCallList(primitives_.sphere());
                break;
            }
            case Shape::Cylinder: {
                applyRotation(drawOrientation);
                glScalef(body.cylinderRadius, body.halfExtents.y, body.cylinderRadius);
                glCallList(primitives_.cylinder());
                break;
            }
            case Shape::Capsule: {
                const float r = body.capsuleRadius;
                const float halfHeight = body.capsuleHeight * 0.5f;
                applyRotation(drawOrientation);
                const float toTop[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, halfHeight, 0, 1};
                glMultMatrixf(toTop);
                glScalef(r, r, r);
                glCallList(primitives_.sphere());
                const float toBottom[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0,
                                             0, -halfHeight * 2.0f, 0, 1};
                glMultMatrixf(toBottom);
                glScalef(r, r, r);
                glCallList(primitives_.sphere());
                glScalef(1.0f / r, 1.0f / r, 1.0f / r);
                glScalef(1.0f, halfHeight, 1.0f);
                glCallList(primitives_.cylinder());
                break;
            }
            case Shape::Cone: {
                applyRotation(drawOrientation);
                glScalef(body.coneRadius, body.coneHeight * 0.5f, body.coneRadius);
                glCallList(primitives_.cone());
                break;
            }
            case Shape::Pyramid:
            case Shape::Prism: {
                const float radius =
                    (body.shape == Shape::Pyramid) ? body.pyramidBaseRadius : body.prismRadius;
                const float height =
                    (body.shape == Shape::Pyramid) ? body.pyramidHeight : body.prismHeight;
                applyRotation(drawOrientation);
                glScalef(radius, height * 0.5f, radius);
                glCallList(primitives_.pyramid());
                break;
            }
            case Shape::Camera: {
                // A camera is drawn as what it is: a body with a lens, and the
                // frustum it looks through. The frustum is the part that says
                // something - a box on its own is a box.
                drawCameraEntity(body, drawOrientation);
                break;
            }
            default: {
                applyRotation(drawOrientation);
                glScalef(body.halfExtents.x, body.halfExtents.y, body.halfExtents.z);
                glCallList(primitives_.cube());
                break;
            }
        }
        }
        glPopMatrix();
        ++paletteIndex;
    }
}

void SceneRenderer::drawCameraEntity(const RigidBody& body, const Quat& orientation) {
    // The body: a small box, the way a camera is drawn everywhere else. The
    // palette colour is already in the GL colour, so the box takes it.
    applyRotation(orientation);
    glScalef(body.halfExtents.x, body.halfExtents.y, body.halfExtents.z);
    glCallList(primitives_.cube());

    // The lens: a wireframe frustum from the front of the body to the far
    // plane. Immediate mode rather than a display list, because its size
    // depends on the field of view, which is a per-camera value.
    //
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
