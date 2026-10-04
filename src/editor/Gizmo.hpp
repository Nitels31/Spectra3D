#pragma once
#include "../core/Math.hpp"
#include "../physics/RigidBody.hpp"
#include "../render/Camera.hpp"

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

// A stable orthonormal pair spanning the plane normal to `axis`. The rotate rings
// are drawn in this frame and picked in the same one, so a ring can never be
// grabbed somewhere it was not drawn.
void ringFrame(const Vec3& axis, Vec3& u, Vec3& v);

// Returns the axis index (0..2) whose handle sits under the pointer, or -1.
// Handles are resolved by screen-space distance so the thin cones stay easy to
// grab. `pointerX`/`pointerY` are viewport-local pixels; the projection basis,
// tangents and eye position are the caller's cached per-frame values, so
// picking costs nothing beyond the three ray/axis tests.
int pickAxis(float pointerX, float pointerY, const Vec3& rayOrigin, const Vec3& rayDir,
             const Vec3& cameraPos, const Vec3& forward, const Vec3& right, const Vec3& up,
             float tanX, float tanY, const Rect& viewport, const RigidBody& body, Mode mode);

// Picking for Mode::Rotate. A ring has no thickness to hit-test against a ray, so
// each one is sampled and measured in screen space; the closest curve within
// kRingPickRadius wins. Runs on a press only, so the samples cost nothing.
int pickRing(float pointerX, float pointerY, const Vec3& rayOrigin, const Vec3& rayDir,
             const Vec3& cameraPos, const Vec3& forward, const Vec3& right, const Vec3& up,
             float tanX, float tanY, const Rect& viewport, const RigidBody& body);

// Screen radius the rotate rings are drawn and picked at. Shared so the drawn
// ring and the grabbable ring are the same size.
float ringRadius(const RigidBody& body);

} // namespace gizmo
} // namespace s3d