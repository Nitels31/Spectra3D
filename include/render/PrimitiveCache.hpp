#pragma once
#include <vector>

#include <GL/gl.h>

#include "core/Math.hpp"

namespace s3d {

// Unit-scaled primitives baked once into GL display lists. Keeping the
// immediate-mode geometry out of the frame loop turns thousands of per-frame
// glVertex calls into a handful of glCallList calls.
//
// The lists record a single glDrawArrays over a vertex array, not a stream of
// immediate-mode commands. GL_COMPILE stores the commands, so a command-stream
// list is re-dispatched in full on every glCallList; a vertex-array list is one
// draw. That difference is the whole reason 200 sphere bodies used to issue
// ~140k GL calls per frame.
//
// The vertex buffers are members, not locals: a display list keeps the client
// pointers, not a copy of the data, so the buffer must outlive the list.
//
// There is no arena here either: the world has no box around it, and the
// shapes are the shapes the scenes are made of.
class PrimitiveCache {
public:
    ~PrimitiveCache();

    PrimitiveCache() = default;
    PrimitiveCache(const PrimitiveCache&) = delete;
    PrimitiveCache& operator=(const PrimitiveCache&) = delete;

    // Builds every unit primitive.
    void build();

    // Releases the display lists. Safe to call twice.
    void release();

    unsigned cube() const { return cube_; }
    unsigned sphere() const { return sphere_; }
    unsigned cylinder() const { return cylinder_; }
    unsigned cone() const { return cone_; }
    unsigned pyramid() const { return pyramid_; }

private:
    void buildCube();
    void buildSphere();
    void buildCylinder();
    void buildCone();
    void buildPyramid(int sides);

    unsigned cube_ = 0;
    unsigned sphere_ = 0;
    unsigned cylinder_ = 0;
    unsigned cone_ = 0;
    unsigned pyramid_ = 0;

    // Interleaved x,y,z,nx,ny,nz for the lit primitives. Built once in build()
    // and kept for the display lists to read on every replay.
    std::vector<float> cubeVerts_;
    std::vector<float> sphereVerts_;
    std::vector<float> cylinderVerts_;
    std::vector<float> coneVerts_;
    std::vector<float> pyramidVerts_;
};

// Immediate-mode line helpers shared by the debug draw paths.
namespace lines {

void arrow(const Vec3& from, const Vec3& to, const Vec3& color);
Vec3 axisVector(int axis, float value);

} // namespace lines

} // namespace s3d
