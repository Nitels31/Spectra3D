#include "render/SceneRenderer.hpp"

#include "core/Math.hpp"

#include <algorithm>

namespace s3d {
namespace {

constexpr double kNearPlane = 0.1;
constexpr double kFarPlane = 900.0;

constexpr float kGridExtent = 100.0f;
constexpr float kMajorStep = 10.0f;
constexpr float kMinorStep = 1.0f;

// Viewport background. Anything the grid and the bodies do not cover ends up in
// this colour.
constexpr float kSkyRed = 0.18f, kSkyGreen = 0.22f, kSkyBlue = 0.28f;

// x, y, z, r, g, b, a per grid vertex in the colour stream.
constexpr int kFloatsPerVertex = 4;

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

void SceneRenderer::setProjection(const Camera& camera, const Viewport& viewport) {
    const double halfHeight = std::tan(camera.fovDeg * kPi / 360.0) * kNearPlane;
    const double halfWidth = halfHeight * (double)viewport.width / (double)viewport.height;

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
                                 const Viewport& viewport, bool showGrid,
                                 bool showVectors, float alpha, int hiddenBody) {
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
    arenaLines_.clear();
    arenaPositions_.clear();
    arenaPositions_.reserve((size_t)ArenaGrid::kVertexCount * 3);

    auto emit = [&](float x, float y, float z) {
        arenaPositions_.push_back(x);
        arenaPositions_.push_back(y);
        arenaPositions_.push_back(z);
    };

    // Both line directions and the per-line RGB are constant for the lifetime
    // of the scene, so they are baked once here. Only alpha depends on the
    // camera and is refreshed each frame.
    for (int i = 0; i < ArenaGrid::kSteps; ++i) {
        const float v = -kGridExtent + (float)i * kMinorStep;
        if (std::fabs(v) < 0.01f) continue; // the world axes are drawn separately
        const bool major = std::fmod(std::fabs(v), kMajorStep) < 0.01f;

        const float base = major ? 0.38f : 0.28f;
        arenaLines_.push_back(v);
        arenaLines_.push_back(base);
        arenaLines_.push_back(base + 0.04f);
        arenaLines_.push_back(base + 0.02f);
        arenaLines_.push_back(major ? 1.0f : 0.0f);

        emit(v, 0.0f, -kGridExtent);
        emit(v, 0.0f, kGridExtent);
        emit(-kGridExtent, 0.0f, v);
        emit(kGridExtent, 0.0f, v);
    }

    // World axes: X red, Y green, Z blue. Same vertex order as before, with the
    // Y axis appended so the blend order of X and Z is untouched.
    emit(-kGridExtent, 0.0f, 0.0f);
    emit(kGridExtent, 0.0f, 0.0f);
    emit(0.0f, 0.0f, -kGridExtent);
    emit(0.0f, 0.0f, kGridExtent);
    emit(0.0f, -kGridExtent, 0.0f);
    emit(0.0f, kGridExtent, 0.0f);

    arenaColors_.assign(arenaPositions_.size() / 3 * 4, 0.0f);
}

void SceneRenderer::drawArena(const Camera& camera) {
    if (!arenaGridReady_) buildArenaGrid();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glLineWidth(1.0f);

    // Fade the whole grid out when the camera flies far above it, then fade
    // individual lines near the camera edge of that range.
    const float cameraDistXZ = std::sqrt(camera.position.x * camera.position.x +
                                         camera.position.z * camera.position.z);
    const float fadeDistance = kGridExtent * 0.9f;
    const float globalAlpha =
        cameraDistXZ < fadeDistance
            ? 1.0f
            : std::fmax(0.0f, 1.0f - (cameraDistXZ - fadeDistance) / (kGridExtent - fadeDistance));

    const float minorAlpha = 0.15f * globalAlpha;
    const float majorAlpha = 0.35f * globalAlpha;
    const float axisAlpha = 0.6f * globalAlpha;

    // camera.position.z squared is loop invariant: it was recomputed 200 times
    // per frame inside the old per-line loop.
    const float cameraZ = camera.position.z;
    const float cameraZSq = cameraZ * cameraZ;
    const float cameraX = camera.position.x;

    // Bounds come from the built arrays, not from the constants, so the fill and
    // the draw can never disagree.
    const int lineCount = (int)(arenaLines_.size() / ArenaGrid::kLineStride);
    const int gridVertices = lineCount * 4;

    const float* lines = arenaLines_.data();
    float* colors = arenaColors_.data();
    for (int i = 0; i < lineCount; ++i, lines += ArenaGrid::kLineStride) {
        const float dx = cameraX - lines[0];
        const float distFromCamera = std::sqrt(dx * dx + cameraZSq);
        float lineAlpha = distFromCamera < fadeDistance
                              ? 1.0f
                              : std::fmax(0.0f, 1.0f - (distFromCamera - fadeDistance) / 20.0f);
        lineAlpha *= globalAlpha;

        const float r = lines[1], g = lines[2], b = lines[3];
        const float alpha = (lines[4] != 0.0f ? majorAlpha : minorAlpha) * lineAlpha;
        float* c = colors + (size_t)i * 4 * kFloatsPerVertex;
        for (int q = 0; q < 4; ++q) {
            c[0] = r; c[1] = g; c[2] = b; c[3] = alpha;
            c += kFloatsPerVertex;
        }
    }

    const float distX = std::fabs(cameraX);
    const float alphaX = distX < fadeDistance
                             ? axisAlpha
                             : axisAlpha * std::fmax(0.0f, 1.0f - (distX - fadeDistance) / 20.0f);
    const float distZ = std::fabs(cameraZ);
    const float alphaZ = distZ < fadeDistance
                             ? axisAlpha
                             : axisAlpha * std::fmax(0.0f, 1.0f - (distZ - fadeDistance) / 20.0f);

    float* axes = colors + (size_t)gridVertices * kFloatsPerVertex;
    axes[0] = axes[4] = 0.75f;
    axes[1] = axes[5] = 0.25f;
    axes[2] = axes[6] = 0.20f;
    axes[3] = axes[7] = alphaX;
    axes[8] = axes[12] = 0.20f;
    axes[9] = axes[13] = 0.45f;
    axes[10] = axes[14] = 0.75f;
    axes[11] = axes[15] = alphaZ;
    // Y axis (green). It is vertical, so there is no distance to fade by - it
    // gets the same alpha as the other axes.
    axes[16] = axes[20] = 0.35f;
    axes[17] = axes[21] = 0.85f;
    axes[18] = axes[22] = 0.40f;
    axes[19] = axes[23] = axisAlpha;

    // One draw call for the whole grid. Lighting is disabled here, so the
    // per-vertex colour array is used directly, exactly as glColor4f +
    // glVertex3f immediate mode used it - and the vertex/segment order is
    // unchanged, which matters because the grid is alpha blended.
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 12, arenaPositions_.data());
    glColorPointer(4, GL_FLOAT, 16, arenaColors_.data());
    glDrawArrays(GL_LINES, 0, (GLsizei)(arenaPositions_.size() / 3));
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
