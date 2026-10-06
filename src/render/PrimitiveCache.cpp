#include "render/PrimitiveCache.hpp"

namespace s3d {
namespace {

constexpr int kArenaWallCount = 6;

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
    const float p = 1.0f;
    glNewList(cube_, GL_COMPILE);
    glBegin(GL_QUADS);
    glNormal3f(1, 0, 0);
    glVertex3f(p, -p, -p); glVertex3f(p, p, -p); glVertex3f(p, p, p); glVertex3f(p, -p, p);
    glNormal3f(-1, 0, 0);
    glVertex3f(-p, -p, -p); glVertex3f(-p, -p, p); glVertex3f(-p, p, p); glVertex3f(-p, p, -p);
    glNormal3f(0, 1, 0);
    glVertex3f(-p, p, -p); glVertex3f(-p, p, p); glVertex3f(p, p, p); glVertex3f(p, p, -p);
    glNormal3f(0, -1, 0);
    glVertex3f(-p, -p, -p); glVertex3f(p, -p, -p); glVertex3f(p, -p, p); glVertex3f(-p, -p, p);
    glNormal3f(0, 0, 1);
    glVertex3f(-p, -p, p); glVertex3f(p, -p, p); glVertex3f(p, p, p); glVertex3f(-p, p, p);
    glNormal3f(0, 0, -1);
    glVertex3f(-p, -p, -p); glVertex3f(-p, p, -p); glVertex3f(p, p, -p); glVertex3f(p, -p, -p);
    glEnd();
    glEndList();
}

void PrimitiveCache::buildSphere() {
    const int stacks = 10, slices = 16;
    glNewList(sphere_, GL_COMPILE);
    for (int i = 0; i < stacks; ++i) {
        const float t0 = kPi * (float)i / (float)stacks;
        const float t1 = kPi * (float)(i + 1) / (float)stacks;
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= slices; ++j) {
            const float phi = 2.0f * kPi * (float)j / (float)slices;
            const float cx = std::cos(phi), sx = std::sin(phi);
            const Vec3 a{std::sin(t0) * cx, std::cos(t0), std::sin(t0) * sx};
            const Vec3 b{std::sin(t1) * cx, std::cos(t1), std::sin(t1) * sx};
            glNormal3f(a.x, a.y, a.z);
            glVertex3f(a.x, a.y, a.z);
            glNormal3f(b.x, b.y, b.z);
            glVertex3f(b.x, b.y, b.z);
        }
        glEnd();
    }
    glEndList();
}

void PrimitiveCache::buildCylinder() {
    const int slices = 24;
    glNewList(cylinder_, GL_COMPILE);
    for (int i = 0; i < slices; ++i) {
        const float a0 = 2.0f * kPi * (float)i / (float)slices;
        const float a1 = 2.0f * kPi * (float)(i + 1) / (float)slices;
        const float c0 = std::cos(a0), s0 = std::sin(a0);
        const float c1 = std::cos(a1), s1 = std::sin(a1);
        glBegin(GL_QUADS);
        glNormal3f(c0, 0.0f, s0);
        glVertex3f(c0, -1.0f, s0);
        glNormal3f(c1, 0.0f, s1);
        glVertex3f(c1, -1.0f, s1);
        glNormal3f(c1, 0.0f, s1);
        glVertex3f(c1, 1.0f, s1);
        glNormal3f(c0, 0.0f, s0);
        glVertex3f(c0, 1.0f, s0);
        glEnd();
    }
    glBegin(GL_TRIANGLE_FAN);
    glNormal3f(0, -1, 0);
    glVertex3f(0, -1, 0);
    for (int i = slices; i >= 0; --i) {
        const float a = 2.0f * kPi * (float)i / (float)slices;
        glVertex3f(std::cos(a), -1.0f, std::sin(a));
    }
    glEnd();
    glBegin(GL_TRIANGLE_FAN);
    glNormal3f(0, 1, 0);
    glVertex3f(0, 1, 0);
    for (int i = 0; i <= slices; ++i) {
        const float a = 2.0f * kPi * (float)i / (float)slices;
        glVertex3f(std::cos(a), 1.0f, std::sin(a));
    }
    glEnd();
    glEndList();
}

void PrimitiveCache::buildCone() {
    const int slices = 24;
    glNewList(cone_, GL_COMPILE);
    for (int i = 0; i < slices; ++i) {
        const float a0 = 2.0f * kPi * (float)i / (float)slices;
        const float a1 = 2.0f * kPi * (float)(i + 1) / (float)slices;
        const float c0 = std::cos(a0), s0 = std::sin(a0);
        const float c1 = std::cos(a1), s1 = std::sin(a1);
        const float nx = std::cos((a0 + a1) * 0.5f);
        const float nz = std::sin((a0 + a1) * 0.5f);
        const float slope = 0.5f;
        glBegin(GL_TRIANGLES);
        glNormal3f(nx, slope, nz);
        glVertex3f(0, 1.0f, 0);
        glNormal3f(c0, slope, s0);
        glVertex3f(c0, -1.0f, s0);
        glNormal3f(c1, slope, s1);
        glVertex3f(c1, -1.0f, s1);
        glEnd();
    }
    glBegin(GL_TRIANGLE_FAN);
    glNormal3f(0, -1, 0);
    glVertex3f(0, -1, 0);
    for (int i = slices; i >= 0; --i) {
        const float a = 2.0f * kPi * (float)i / (float)slices;
        glVertex3f(std::cos(a), -1.0f, std::sin(a));
    }
    glEnd();
    glEndList();
}

void PrimitiveCache::buildPyramid(int sides) {
    glNewList(pyramid_, GL_COMPILE);
    const float topY = 1.0f, bottomY = -1.0f;
    for (int i = 0; i < sides; ++i) {
        const float a0 = 2.0f * kPi * (float)i / (float)sides;
        const float a1 = 2.0f * kPi * (float)(i + 1) / (float)sides;
        const Vec3 base0{std::cos(a0), bottomY, std::sin(a0)};
        const Vec3 base1{std::cos(a1), bottomY, std::sin(a1)};
        const Vec3 apex{0.0f, topY, 0.0f};
        const Vec3 normal = cross(base1 - base0, apex - base0).normalized();
        glBegin(GL_TRIANGLES);
        glNormal3f(normal.x, normal.y, normal.z);
        glVertex3f(base0.x, base0.y, base0.z);
        glVertex3f(base1.x, base1.y, base1.z);
        glVertex3f(apex.x, apex.y, apex.z);
        glEnd();
    }
    glBegin(GL_TRIANGLE_FAN);
    glNormal3f(0, -1, 0);
    glVertex3f(0, bottomY, 0);
    for (int i = sides; i >= 0; --i) {
        const float a = 2.0f * kPi * (float)i / (float)sides;
        glVertex3f(std::cos(a), bottomY, std::sin(a));
    }
    glEnd();
    glEndList();
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

    glNewList(wallGrid_[wall], GL_COMPILE);
    glBegin(GL_LINES);
    for (float gv = component(mn, v) + stepV; gv < component(mx, v) - 1e-3f; gv += stepV) {
        const Vec3 p1 = pointAt(component(mn, u), gv);
        const Vec3 p2 = pointAt(component(mx, u), gv);
        glVertex3f(p1.x, p1.y, p1.z);
        glVertex3f(p2.x, p2.y, p2.z);
    }
    for (float gu = component(mn, u) + stepU; gu < component(mx, u) - 1e-3f; gu += stepU) {
        const Vec3 p1 = pointAt(gu, component(mn, v));
        const Vec3 p2 = pointAt(gu, component(mx, v));
        glVertex3f(p1.x, p1.y, p1.z);
        glVertex3f(p2.x, p2.y, p2.z);
    }
    glEnd();
    glEndList();
}

void PrimitiveCache::buildArenaEdges(const Vec3& mn, const Vec3& mx) {
    glNewList(arenaEdges_, GL_COMPILE);
    glBegin(GL_LINES);
    Vec3 corners[8];
    for (int k = 0; k < 8; ++k)
        corners[k] = lines::axisVector(0, (k & 1) ? mx.x : mn.x) +
                     lines::axisVector(1, (k & 2) ? mx.y : mn.y) +
                     lines::axisVector(2, (k & 4) ? mx.z : mn.z);
    for (int k = 0; k < 8; ++k)
        for (int bit = 0; bit < 3; ++bit) {
            const int j = k ^ (1 << bit);
            if (j <= k) continue;
            glVertex3f(corners[k].x, corners[k].y, corners[k].z);
            glVertex3f(corners[j].x, corners[j].y, corners[j].z);
        }
    glEnd();
    glEndList();
}

} // namespace s3d