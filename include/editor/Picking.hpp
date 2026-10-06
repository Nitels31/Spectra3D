#pragma once
#include "core/Math.hpp"
#include "physics/PhysicsWorld.hpp"
#include "render/Camera.hpp"

namespace s3d {
namespace picking {

// Slab test against the body's oriented box. Writes the entry distance and
// returns false when the ray misses.
bool intersectObb(const Vec3& origin, const Vec3& direction, const RigidBody& body,
                  float& hitDistance);

// Ray vs sphere centred at the body position.
bool intersectSphere(const Vec3& origin, const Vec3& direction, const RigidBody& body,
                     float& hitDistance);

// Shape dispatch over the two primitives above.
bool intersectBody(const Vec3& origin, const Vec3& direction, const RigidBody& body,
                   float& hitDistance);

// Nearest body hit along the ray, or -1.
int pickBody(const Vec3& origin, const Vec3& direction, const PhysicsWorld& world);

// Closest point approach between a ray and an infinite axis through `p0`.
// Writes the axis parameter in `t` and the ray parameter in `s`.
void closestOnAxis(const Vec3& origin, const Vec3& direction, const Vec3& p0,
                   const Vec3& axis, float& t, float& s);

// Projects a world point into viewport pixel coordinates. Returns false when the
// point is behind the camera.
bool projectToViewport(const Vec3& worldPoint, const Vec3& cameraPos, const Vec3& forward,
                       const Vec3& right, const Vec3& up, float tanX, float tanY,
                       float width, float height, float& outX, float& outY);

// Builds a world-space ray from a pixel inside the viewport.
Vec3 rayFromViewport(float pixelX, float pixelY, const Rect& viewport,
                     const Vec3& forward, const Vec3& right, const Vec3& up, float tanX,
                     float tanY);

} // namespace picking
} // namespace s3d