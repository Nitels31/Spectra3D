#pragma once

#include "physics/RigidBody.hpp"
#include "render/Camera.hpp"

namespace s3d {

// A camera in the scene, and the two ways it can be said.
//
// A body is a position and a quaternion; the engine's view camera is a position,
// a yaw and a pitch. A camera entity is both at once - it is placed like a body
// and looked through like a camera - so something has to translate between the
// two, and this is that something.
//
// The convention is the engine's own: a camera looks down its local -Z, which
// is where the view camera looks at yaw 0. A camera body therefore points the
// same way as the free camera would if it were placed there.

// The view camera that looks through `body`. The field of view comes from the
// body, so a camera entity carries its own lens.
Camera cameraFromEntity(const RigidBody& body);

// The orientation a body needs to look the way `camera` looks. The inverse of
// cameraFromEntity, and the same thing round the other way.
Quat orientationFromCamera(const Camera& camera);

// The direction a camera entity looks, in world space.
Vec3 forwardFromEntity(const RigidBody& body);

} // namespace s3d
