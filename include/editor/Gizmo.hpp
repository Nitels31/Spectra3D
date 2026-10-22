#pragma once
#include "core/Math.hpp"
#include "editor/Picking.hpp"
#include "physics/RigidBody.hpp"
#include "render/Camera.hpp"

namespace s3d {
namespace gizmo {

// The three manipulation modes the gizmo can actually perform. Each one has its
// own handles, its own picking and its own drag maths - a toolbar button that
// only renamed a mode without any of that would be decoration.
enum class Mode { Move = 0, Rotate = 1, Scale = 2 };

// Gizmo orientation. Scale handles follow the body's local axes so they rotate
// with it; move handles stay world aligned.
void axesFor(const RigidBody& body, Mode mode, Vec3 out[3]);

float handleLength(const RigidBody& body);

// A stable orthonormal pair spanning the plane normal to `axis`. The rotate
// rings are drawn in this frame and picked in the same one, so a ring can never
// be grabbed somewhere it was not drawn.
void ringFrame(const Vec3& axis, Vec3& u, Vec3& v);

// Returns the axis index (0..2) whose handle sits under the pointer, or -1.
// Handles are resolved by screen-space distance so the thin cones stay easy to
// grab. `pointerX`/`pointerY` are viewport-local pixels; the projection is
// the caller's cached per-frame value, so picking costs nothing beyond the
// three ray/axis tests.
//
// `flatAxis` is the 2D view: the world axis the picture looks down, or -1 for
// the 3D one. A flat world has two directions to move in and one to turn
// about, so the third handle is neither drawn nor picked.
int pickAxis(float pointerX, float pointerY, const Vec3& rayOrigin, const Vec3& rayDir,
             const picking::ViewProjection& projection, const Rect& viewport,
             const RigidBody& body, Mode mode, int flatAxis);

// Picking for Mode::Rotate. A ring has no thickness to hit-test against a ray, so
// each one is sampled and measured in screen space; the closest curve within
// kRingPickRadius wins. Runs on a press only, so the samples cost nothing.
//
// In the 2D view the only ring is the one about the axis the picture looks
// down: a flat world turns in its own plane and nowhere else.
int pickRing(float pointerX, float pointerY, const Vec3& rayOrigin, const Vec3& rayDir,
             const picking::ViewProjection& projection, const Rect& viewport,
             const RigidBody& body, int flatAxis);

// The axes of the gizmo that are drawn and picked, and how many of them.
//
// The 3D view has all three. A flat picture has two directions in it and one
// out of it, and the handle for the third one is neither drawn nor picked: a
// handle that is not drawn must not be grabbable.
//
// The move handles are the world's axes, so the one out of the picture is the
// flat axis itself. The scale handles are the body's own axes, and the one out
// of the picture is the one whose world direction is most along it.
//
// `out` is filled with the indices of the axes to draw and their number is
// returned. `flatAxis` is -1 for the 3D view, where all three are drawn.
int flatDrawAxes(const Vec3 axes[3], int flatAxis, int out[3]);

// The axis of `axes` that points out of the flat picture, or -1 when the
// picture is not flat.
int flatOutOfPlaneAxis(const Vec3 axes[3], int flatAxis);

// Screen radius the rotate rings are drawn and picked at. Shared so the drawn
// ring and the grabbable ring are the same size.
float ringRadius(const RigidBody& body);

} // namespace gizmo
} // namespace s3d