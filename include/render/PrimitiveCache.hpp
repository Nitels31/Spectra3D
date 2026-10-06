#pragma once
#include <vector>

#include <GL/gl.h>

#include "core/Math.hpp"

namespace s3d {

// Unit-scaled primitives baked once into GL display lists. Keeping the
// immediate-mode geometry out of the frame loop turns thousands of per-frame
// glVertex calls into a handful of glCallList calls.
class PrimitiveCache {
public:
    ~PrimitiveCache();

    PrimitiveCache() = default;
    PrimitiveCache(const PrimitiveCache&) = delete;
    PrimitiveCache& operator=(const PrimitiveCache&) = delete;

    // Builds every unit primitive plus the arena grid/edge lists for `bounds`.
    void build(const Vec3& boundsMin, const Vec3& boundsMax);

    // Releases the display lists. Safe to call twice.
    void release();

    unsigned cube() const { return cube_; }
    unsigned sphere() const { return sphere_; }
    unsigned cylinder() const { return cylinder_; }
    unsigned cone() const { return cone_; }
    unsigned pyramid() const { return pyramid_; }
    unsigned wallGrid(int wall) const { return wallGrid_[wall]; }
    unsigned arenaEdges() const { return arenaEdges_; }

private:
    void buildCube();
    void buildSphere();
    void buildCylinder();
    void buildCone();
    void buildPyramid(int sides);
    void buildWallGrid(int wall, const Vec3& mn, const Vec3& mx);
    void buildArenaEdges(const Vec3& mn, const Vec3& mx);

    unsigned base_ = 0;
    unsigned cube_ = 0;
    unsigned wallGrid_[6] = {};
    unsigned arenaEdges_ = 0;
    unsigned sphere_ = 0;
    unsigned cylinder_ = 0;
    unsigned cone_ = 0;
    unsigned pyramid_ = 0;
};

// Immediate-mode line helpers shared by the debug draw paths.
namespace lines {

void arrow(const Vec3& from, const Vec3& to, const Vec3& color);
Vec3 axisVector(int axis, float value);

} // namespace lines

} // namespace s3d