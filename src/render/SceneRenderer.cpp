#include "SceneRenderer.hpp"

#include "../core/Math.hpp"

#include <algorithm>

namespace s3d {
namespace {

constexpr double kNearPlane = 0.1;
constexpr double kFarPlane = 900.0;

// Fallback colors for bodies without an explicit material color.
const Color kBodyPalette[8] = {{0.86f, 0.36f, 0.33f}, {0.36f, 0.62f, 0.90f},
                               {0.95f, 0.76f, 0.31f}, {0.46f, 0.79f, 0.45f},
                               {0.72f, 0.49f, 0.86f}, {0.91f, 0.53f, 0.26f},
                               {0.35f, 0.75f, 0.73f}, {0.83f, 0.83f, 0.87f}};

void toColumnMajor(const Quat& q, float* m) {
    const Mat3 r = Mat3::fromQuat(q);
    for (int c = 0; c < 3; ++c)
        for (int row = 0; row < 3; ++row)
            m[c * 4 + row] = r.m[row * 3 + c];
    m[3] = m[7] = m[11] = 0.0f;
    m[12] = m[13] = m[14] = 0.0f;
    m[15] = 1.0f;
}

void applyRotation(const Quat& q) {
    float m[16];
    toColumnMajor(q, m);
    glMultMatrixf(m);
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

    primitives_.build(arenaMin, arenaMax);
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

void SceneRenderer::renderWorld(const PhysicsWorld& world, const MeshRegistry& meshes,
                                const Camera& camera, const Viewport& viewport,
                                bool showGrid, bool showVectors) {
    if (viewport.width < 1 || viewport.height < 1) return;

    glEnable(GL_SCISSOR_TEST);
    glScissor(viewport.glX, viewport.glY, viewport.width, viewport.height);
    setProjection(camera, viewport);

    glClearColor(0.18f, 0.22f, 0.28f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    setViewMatrix(camera);
    glDisable(GL_LIGHTING);
    extractFrustum(projection_, view_);

    if (showGrid) drawArena(camera);
    drawBodies(world, meshes);

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

void SceneRenderer::drawArena(const Camera& camera) {
    constexpr float kGridExtent = 100.0f;
    constexpr float kMajorStep = 10.0f;
    constexpr float kMinorStep = 1.0f;

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

    glBegin(GL_LINES);
    for (float v = -kGridExtent; v <= kGridExtent; v += kMinorStep) {
        if (std::fabs(v) < 0.01f) continue; // the world axes are drawn separately
        const bool major = std::fmod(std::fabs(v), kMajorStep) < 0.01f;

        const float dx = camera.position.x - v;
        const float distFromCamera = std::sqrt(dx * dx +
                                                camera.position.z * camera.position.z);
        float lineAlpha = distFromCamera < fadeDistance
                              ? 1.0f
                              : std::fmax(0.0f, 1.0f - (distFromCamera - fadeDistance) / 20.0f);
        lineAlpha *= globalAlpha;

        const float base = major ? 0.38f : 0.28f;
        const float alpha = (major ? majorAlpha : minorAlpha) * lineAlpha;

        glColor4f(base, base + 0.04f, base + 0.02f, alpha);
        glVertex3f(v, 0.0f, -kGridExtent);
        glVertex3f(v, 0.0f, kGridExtent);
        glVertex3f(-kGridExtent, 0.0f, v);
        glVertex3f(kGridExtent, 0.0f, v);
    }

    const float distX = std::fabs(camera.position.x);
    const float alphaX = distX < fadeDistance
                             ? axisAlpha
                             : axisAlpha * std::fmax(0.0f, 1.0f - (distX - fadeDistance) / 20.0f);
    const float distZ = std::fabs(camera.position.z);
    const float alphaZ = distZ < fadeDistance
                             ? axisAlpha
                             : axisAlpha * std::fmax(0.0f, 1.0f - (distZ - fadeDistance) / 20.0f);

    glColor4f(0.75f, 0.25f, 0.20f, alphaX);
    glVertex3f(-kGridExtent, 0.0f, 0.0f);
    glVertex3f(kGridExtent, 0.0f, 0.0f);
    glColor4f(0.20f, 0.45f, 0.75f, alphaZ);
    glVertex3f(0.0f, 0.0f, -kGridExtent);
    glVertex3f(0.0f, 0.0f, kGridExtent);
    glEnd();

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void SceneRenderer::drawDebugVectors(const PhysicsWorld& world) {
    glLineWidth(2.0f);
    lines::arrow(Vec3{0.0f, 0.0f, 0.0f}, world.gravity.normalized() * 6.0f,
                 Vec3{1.0f, 0.30f, 0.25f});
    glLineWidth(1.0f);
}

bool SceneRenderer::drawImportedMesh(const RigidBody& body, const MeshRegistry& meshes) {
    if (!body.hasMesh()) return false;
    const MeshAsset* mesh = meshes.at(body.mesh);
    if (!mesh) return false;

    const float scale = body.meshScale * mesh->scale;
    glPushMatrix();
    glTranslatef(body.position.x, body.position.y, body.position.z);
    applyRotation(body.orientation);
    glScalef(scale, scale, scale);
    glTranslatef(-mesh->center.x, -mesh->center.y, -mesh->center.z);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    for (const MeshPart& part : mesh->parts) {
        glColor3f(part.color[0], part.color[1], part.color[2]);
        glVertexPointer(3, GL_FLOAT, 24, part.vertices.data());
        glNormalPointer(GL_FLOAT, 24, part.vertices.data() + 3);
        if (!part.indices.empty()) {
            glDrawElements(GL_TRIANGLES, (GLsizei)part.indices.size(), GL_UNSIGNED_INT,
                           part.indices.data());
        } else {
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(part.vertices.size() / 6));
        }
    }
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glPopMatrix();
    return true;
}

void SceneRenderer::drawBodies(const PhysicsWorld& world, const MeshRegistry& meshes) {
    int paletteIndex = 0;
    for (const RigidBody& body : world.bodies) {
        if (!body.visible) continue;
        if (!sphereVisible(body.position, body.boundRadius)) continue;

        const bool hasExplicitColor = body.material.color.x != 0.0f ||
                                      body.material.color.y != 0.0f ||
                                      body.material.color.z != 0.0f;
        const Color color = hasExplicitColor
                                ? Color{body.material.color.x, body.material.color.y,
                                        body.material.color.z}
                                : kBodyPalette[paletteIndex & 7];
        glColor3f(color.r, color.g, color.b);

        glPushMatrix();
        glTranslatef(body.position.x, body.position.y, body.position.z);
        if (drawImportedMesh(body, meshes)) {
            ++paletteIndex;
            continue;
        }

        switch (body.shape) {
            case Shape::Sphere: {
                const float r = body.halfExtents.x;
                glScalef(r, r, r);
                glCallList(primitives_.sphere());
                break;
            }
            case Shape::Cylinder: {
                applyRotation(body.orientation);
                glScalef(body.cylinderRadius, body.halfExtents.y, body.cylinderRadius);
                glCallList(primitives_.cylinder());
                break;
            }
            case Shape::Capsule: {
                const float r = body.capsuleRadius;
                const float halfHeight = body.capsuleHeight * 0.5f;
                applyRotation(body.orientation);
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
                applyRotation(body.orientation);
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
                applyRotation(body.orientation);
                glScalef(radius, height * 0.5f, radius);
                glCallList(primitives_.pyramid());
                break;
            }
            default: {
                applyRotation(body.orientation);
                glScalef(body.halfExtents.x, body.halfExtents.y, body.halfExtents.z);
                glCallList(primitives_.cube());
                break;
            }
        }
        glPopMatrix();
        ++paletteIndex;
    }
}

void SceneRenderer::renderSelection(const RigidBody& body) {
    glDisable(GL_LIGHTING);
    glLineWidth(2.0f);
    glColor3f(1.0f, 0.85f, 0.25f);

    if (body.shape == Shape::Sphere) {
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
                               int activeAxis, bool scaleMode) {
    static const Color kAxisColors[3] = {{1.0f, 0.30f, 0.30f}, {0.35f, 1.0f, 0.35f},
                                         {0.40f, 0.60f, 1.0f}};

    glDisable(GL_LIGHTING);
    glDepthFunc(GL_ALWAYS); // gizmo stays visible through geometry
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

        if (scaleMode) {
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