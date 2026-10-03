#pragma once
#include "../core/Math.hpp"
#include "../physics/RigidBody.hpp"
#include "../render/Camera.hpp"

namespace s3d {
namespace gizmo {

enum class Mode { Move = 0, Scale = 1 };

// Gizmo orientation. Scale handles follow the body's local axes so they rotate
// with it; move handles stay world aligned.
void axesFor(const RigidBody& body, Mode mode, Vec3 out[3]);

float handleLength(const RigidBody& body);

// Returns the axis index (0..2) whose handle sits under the pointer, or -1.
// Handles are resolved by screen-space distance so the thin cones stay easy to
// grab. `pointerX`/`pointerY` are viewport-local pixels.
int pickAxis(float pointerX, float pointerY, const Vec3& rayOrigin, const Vec3& rayDir,
             const Camera& camera, const Rect& viewport, const RigidBody& body, Mode mode);

} // namespace gizmo
} // namespace s3d