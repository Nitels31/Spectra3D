#include "render/PrimitiveCache.hpp"

namespace s3d {
namespace {

constexpr int kArenaWallCount = 6;

// Interleaved position/normal, 6 floats per vertex - the same layout
// drawImportedMesh() and drawArena() already feed to glVertexPointer, so the
// primitives speak the dialect the rest of the renderer uses.
void pushVertex(std::vector<float>& out, const Vec3& position, const Vec3& normal) {
    out.push_back(position.x);
    out.push_back(position.y);
    out.push_back(position.z);
    out.push_back(normal.x);
    out.push_back(normal.y);
    out.push_back(normal.z);
}

// Records ONE glDrawArrays into `list`, reading `verts`.
//
// Why one call and not the old immediate-mode stream: glNewList(GL_COMPILE)
// stores the COMMANDS, not the geometry, so glCallList re-dispatches every one
// of them - a sphere was 700 glNormal/glVertex calls replayed per body per
// frame, which is ~140k GL calls for a 200-body scene. This records a single
// draw that the driver turns into one vertex fetch, so the same body costs one
// call. This is the exact pattern SceneRenderer::drawImportedMesh already uses.
//
// `verts` must outlive the display list: the list holds the client-state
// pointers, not a copy, so the buffer has to still be alive when the list is
// replayed. That is why these are members rather than locals.
void compileVertexList(unsigned list, const std::vector<float>& verts, GLenum mode) {
    glNewList(list, GL_COMPILE);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glVertexPointer(3, GL_FLOAT, 24, verts.data());
    glNormalPointer(GL_FLOAT, 24, verts.data() + 3);
    glDrawArrays(mode, 0, (GLsizei)(verts.size() / 6));
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glEndList();
}

} // namespace

Vec3 lines::axisVector(int axis, float value) {
    if (axis == 0) return {value, 0.0f, 0.0f};
    if (axis == 1) return {0.0f, value, 0.0f};
    return {0.0f, 0.0f, value};
}

void lines::arrow(const Vec3& from, const Vec3& to, const Vec3& color) {
    Vec3 dir = to - from;
    const float length = dir.length();
    if (length < 1e-5f) return;
    dir = dir / length;

    glColor3f(color.x, color.y, color.z);
    glBegin(GL_LINES);
    glVertex3f(from.x, from.y, from.z);
    glVertex3f(to.x, to.y, to.z);
    glEnd();

    Vec3 perp = std::fabs(dir.y) < 0.9f ? cross(dir, Vec3{0.0f, 1.0f, 0.0f})
                                        : cross(dir, Vec3{1.0f, 0.0f, 0.0f});
    perp = perp.normalized();
    const Vec3 binormal = cross(dir, perp);
    const Vec3 back = to - dir * 0.6f;

    glBegin(GL_LINES);
    const Vec3 tips[4] = {perp, -perp, binormal, -binormal};
    for (const Vec3& tip : tips) {
        glVertex3f(to.x, to.y, to.z);
        glVertex3f(back.x + tip.x * 0.25f, back.y + tip.y * 0.25f, back.z + tip.z * 0.25f);
    }
    glEnd();
}

PrimitiveCache::~PrimitiveCache() { release(); }

void PrimitiveCache::release() {
    if (base_) {
        glDeleteLists(base_, 11);
        base_ = 0;
    }
    if (cube_) {
        glDeleteLists(cube_, 1);
        cube_ = 0;
    }
    sphere_ = cylinder_ = cone_ = pyramid_ = arenaEdges_ = 0;
    for (int i = 0; i < kArenaWallCount; ++i) wallGrid_[i] = 0;
}

void PrimitiveCache::build(const Vec3& boundsMin, const Vec3& boundsMax) {
    release();
    buildCube();

    // One contiguous block of 11 lists: 6 wall grids, arena edges, sphere,
    // cylinder, cone, pyramid. Contiguity makes teardown a single call.
    base_ = glGenLists(11);
    if (!base_) return;
    for (int i = 0; i < kArenaWallCount; ++i) wallGrid_[i] = base_ + i;
    arenaEdges_ = base_ + 6;
    sphere_ = base_ + 7;
    cylinder_ = base_ + 8;
    cone_ = base_ + 9;
    pyramid_ = base_ + 10;

    for (int i = 0; i < kArenaWallCount; ++i) buildWallGrid(i, boundsMin, boundsMax);
    buildArenaEdges(boundsMin, boundsMax);
    buildSphere();
    buildCylinder();
    buildCone();
    buildPyramid(4);
}

void PrimitiveCache::buildCube() {
    if (!cube_) cube_ = glGenLists(1);
    // Vertex data is interleaved position/normal (6 floats) and lives in a member
    // that outlives the display list: GL_COMPILE records the *call* to read this
    // buffer, and the buffer has to still be there when glCallList replays it.
    cubeVerts_.clear();
    const float p = 1.0f;
    const Vec3 faces[6][4] = {
        {{p, -p, -p}, {p, p, -p}, {p, p, p}, {p, -p, p}},     // +X
        {{-p, -p, -p}, {-p, -p, p}, {-p, p, p}, {-p, p, -p}}, // -X
        {{-p, p, -p}, {-p, p, p}, {p, p, p}, {p, p, -p}},     // +Y
        {{-p, -p, -p}, {p, -p, -p}, {p, -p, p}, {-p, -p, p}}, // -Y
        {{-p, -p, p}, {p, -p, p}, {p, p, p}, {-p, p, p}},     // +Z
        {{-p, -p, -p}, {-p, p, -p}, {p, p, -p}, {p, -p, -p}}, // -Z
    };
    for (int f = 0; f < 6; ++f) {
        const Vec3 normal{f == 0 ? 1.0f : f == 1 ? -1.0f : 0.0f,
                          f == 2 ? 1.0f : f == 3 ? -1.0f : 0.0f,
                          f == 4 ? 1.0f : f == 5 ? -1.0f : 0.0f};
        for (int v = 0; v < 4; ++v) {
            pushVertex(cubeVerts_, faces[f][v], normal);
        }
    }
    compileVertexList(cube_, cubeVerts_, GL_TRIANGLES);
}

void PrimitiveCache::buildSphere() {
    const int stacks = 10, slices = 16;
    sphereVerts_.clear();
    for (int i = 0; i < stacks; ++i) {
        const float t0 = kPi * (float)i / (float)stacks;
        const float t1 = kPi * (float)(i + 1) / (float)stacks;
        for (int j = 0; j <= slices; ++j) {
            const float phi = 2.0f * kPi * (float)j / (float)slices;
            const float cx = std::cos(phi), sx = std::sin(phi);
            const Vec3 a{std::sin(t0) * cx, std::cos(t0), std::sin(t0) * sx};
            const Vec3 b{std::sin(t1) * cx, std::cos(t1), std::sin(t1) * sx};
            // Sphere normals are the normalised position on the unit sphere, so
            // the same vector is both the vertex and its normal.
            pushVertex(sphereVerts_, a, a);
            pushVertex(sphereVerts_, b, b);
        }
    }
    compileVertexList(sphere_, sphereVerts_, GL_QUAD_STRIP);
}

void PrimitiveCache::buildCylinder() {
    const int slices = 24;
    cylinderVerts_.clear();
    // Side wall: one quad per slice, normals from the rim angle.
    for (int i = 0; i < slices; ++i) {
        const float a0 = 2.0f * kPi * (float)i / (float)slices;
        const float a1 = 2.0f * kPi * (float)(i + 1) / (float)slices;
        const float c0 = std::cos(a0), s0 = std::sin(a0);
        const float c1 = std::cos(a1), s1 = std::sin(a1);
        pushVertex(cylinderVerts_, {c0, -1.0f, s0}, {c0, 0.0f, s0});
        pushVertex(cylinderVerts_, {c1, -1.0f, s1}, {c1, 0.0f, s1});
        pushVertex(cylinderVerts_, {c1, 1.0f, s1}, {c1, 0.0f, s1});
        pushVertex(cylinderVerts_, {c0, -1.0f, s0}, {c0, 0.0f, s0});
        pushVertex(cylinderVerts_, {c1, 1.0f, s1}, {c1, 0.0f, s1});
        pushVertex(cylinderVerts_, {c0, 1.0f, s0}, {c0, 0.0f, s0});
    }
    // Caps: triangle fans with a flat cap normal.
    pushVertex(cylinderVerts_, {0.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f});
    for (int i = slices; i >= 0; --i) {
        const float a = 2.0f * kPi * (float)i / (float)slices;
        pushVertex(cylinderVerts_, {std::cos(a), -1.0f, std::sin(a)},
                   {0.0f, -1.0f, 0.0f});
    }
    pushVertex(cylinderVerts_, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    for (int i = 0; i <= slices; ++i) {
        const float a = 2.0f * kPi * (float)i / (float)slices;
        pushVertex(cylinderVerts_, {std::cos(a), 1.0f, std::sin(a)}, {0.0f, 1.0f, 0.0f});
    }
    compileVertexList(cylinder_, cylinderVerts_, GL_TRIANGLES);
}

void PrimitiveCache::buildCone() {
    const int slices = 24;
    coneVerts_.clear();
    // Side: a triangle per slice, normal tilted by the slope (height 2 over
    // radius 1 gives a 0.5 slope), same as the immediate-mode version.
    for (int i = 0; i < slices; ++i) {
        const float a0 = 2.0f * kPi * (float)i / (float)slices;
        const float a1 = 2.0f * kPi * (float)(i + 1) / (float)slices;
        const float c0 = std::cos(a0), s0 = std::sin(a0);
        const float c1 = std::cos(a1), s1 = std::sin(a1);
        const float nx = std::cos((a0 + a1) * 0.5f);
        const float nz = std::sin((a0 + a1) * 0.5f);
        const float slope = 0.5f;
        pushVertex(coneVerts_, {0.0f, 1.0f, 0.0f}, {nx, slope, nz});
        pushVertex(coneVerts_, {c0, -1.0f, s0}, {c0, slope, s0});
        pushVertex(coneVerts_, {c1, -1.0f, s1}, {c1, slope, s1});
    }
    // Base cap.
    pushVertex(coneVerts_, {0.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f});
    for (int i = slices; i >= 0; --i) {
        const float a = 2.0f * kPi * (float)i / (float)slices;
        pushVertex(coneVerts_, {std::cos(a), -1.0f, std::sin(a)}, {0.0f, -1.0f, 0.0f});
    }
    compileVertexList(cone_, coneVerts_, GL_TRIANGLES);
}

void PrimitiveCache::buildPyramid(int sides) {
    pyramidVerts_.clear();
    const float topY = 1.0f, bottomY = -1.0f;
    // Side: one triangle per side, face normal from the winding.
    for (int i = 0; i < sides; ++i) {
        const float a0 = 2.0f * kPi * (float)i / (float)sides;
        const float a1 = 2.0f * kPi * (float)(i + 1) / (float)sides;
        const Vec3 base0{std::cos(a0), bottomY, std::sin(a0)};
        const Vec3 base1{std::cos(a1), bottomY, std::sin(a1)};
        const Vec3 apex{0.0f, topY, 0.0f};
        const Vec3 normal = cross(base1 - base0, apex - base0).normalized();
        pushVertex(pyramidVerts_, base0, normal);
        pushVertex(pyramidVerts_, base1, normal);
        pushVertex(pyramidVerts_, apex, normal);
    }
    // Base cap.
    pushVertex(pyramidVerts_, {0.0f, bottomY, 0.0f}, {0.0f, bottomY, 0.0f});
    for (int i = sides; i >= 0; --i) {
        const float a = 2.0f * kPi * (float)i / (float)sides;
        pushVertex(pyramidVerts_, {std::cos(a), bottomY, std::sin(a)},
                   {0.0f, bottomY, 0.0f});
    }
    compileVertexList(pyramid_, pyramidVerts_, GL_TRIANGLES);
}

void PrimitiveCache::buildWallGrid(int wall, const Vec3& mn, const Vec3& mx) {
    static const int wallAxis[6] = {0, 0, 1, 1, 2, 2};
    const int a = wallAxis[wall];
    const int u = (a + 1) % 3, v = (a + 2) % 3;
    const float depth = (wall % 2 == 0) ? component(mn, a) : component(mx, a);

    // Large arenas get a coarse grid so the line count stays sane.
    const float stepU = (component(mx, u) - component(mn, u) > 30.0f) ? 5.0f : 1.0f;
    const float stepV = (component(mx, v) - component(mn, v) > 30.0f) ? 5.0f : 1.0f;

    auto pointAt = [&](float uu, float vv) {
        return lines::axisVector(a, depth) + lines::axisVector(u, uu) + lines::axisVector(v, vv);
    };

    // Lines carry no normals, so this one keeps a positions-only buffer.
    wallGridVerts_[wall].clear();
    std::vector<float>& verts = wallGridVerts_[wall];
    for (float gv = component(mn, v) + stepV; gv < component(mx, v) - 1e-3f; gv += stepV) {
        const Vec3 p1 = pointAt(component(mn, u), gv);
        const Vec3 p2 = pointAt(component(mx, u), gv);
        verts.push_back(p1.x); verts.push_back(p1.y); verts.push_back(p1.z);
        verts.push_back(p2.x); verts.push_back(p2.y); verts.push_back(p2.z);
    }
    for (float gu = component(mn, u) + stepU; gu < component(mx, u) - 1e-3f; gu += stepU) {
        const Vec3 p1 = pointAt(gu, component(mn, v));
        const Vec3 p2 = pointAt(gu, component(mx, v));
        verts.push_back(p1.x); verts.push_back(p1.y); verts.push_back(p1.z);
        verts.push_back(p2.x); verts.push_back(p2.y); verts.push_back(p2.z);
    }
    glNewList(wallGrid_[wall], GL_COMPILE);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, verts.data());
    glDrawArrays(GL_LINES, 0, (GLsizei)(verts.size() / 3));
    glDisableClientState(GL_VERTEX_ARRAY);
    glEndList();
}

void PrimitiveCache::buildArenaEdges(const Vec3& mn, const Vec3& mx) {
    arenaEdgesVerts_.clear();
    std::vector<float>& verts = arenaEdgesVerts_;
    Vec3 corners[8];
    for (int k = 0; k < 8; ++k)
        corners[k] = lines::axisVector(0, (k & 1) ? mx.x : mn.x) +
                     lines::axisVector(1, (k & 2) ? mx.y : mn.y) +
                     lines::axisVector(2, (k & 4) ? mx.z : mn.z);
    for (int k = 0; k < 8; ++k)
        for (int bit = 0; bit < 3; ++bit) {
            const int j = k ^ (1 << bit);
            if (j <= k) continue;
            verts.push_back(corners[k].x); verts.push_back(corners[k].y); verts.push_back(corners[k].z);
            verts.push_back(corners[j].x); verts.push_back(corners[j].y); verts.push_back(corners[j].z);
        }
    glNewList(arenaEdges_, GL_COMPILE);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, verts.data());
    glDrawArrays(GL_LINES, 0, (GLsizei)(verts.size() / 3));
    glDisableClientState(GL_VERTEX_ARRAY);
    glEndList();
}

} // namespace s3d