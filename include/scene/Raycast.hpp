#pragma once
#include "core/Math.hpp"
#include "physics/PhysicsWorld.hpp"

namespace s3d {
namespace scene {

// What a ray found.
struct RaycastHit {
    // The index of the body, or -1 when the ray found nothing.
    int body = -1;
    // Where the ray met the body.
    Vec3 point;
    // The face it met, pointing out of the body.
    Vec3 normal;
    // How far along the ray that was.
    float distance = 0.0f;
};

// The ray against the oriented box of a body. Writes the distance and the
// face it met, and returns false when the ray misses.
bool intersectObb(const Vec3& origin, const Vec3& direction, const RigidBody& body,
                  float& distance, Vec3& normal);

// The ray against the sphere of a body.
bool intersectSphere(const Vec3& origin, const Vec3& direction, const RigidBody& body,
                     float& distance, Vec3& normal);

// The shape dispatch of the two above: a sphere is a sphere and everything
// else is a box.
bool intersectBody(const Vec3& origin, const Vec3& direction, const RigidBody& body,
                   float& distance, Vec3& normal);

// The nearest body along the ray, or nothing.
//
// The bodies of the world are the ones the ray is tested against, and the
// first one it meets is the one that is in front of the others - so the ray
// from the eye of the camera does not pick the camera's own body, which is
// where the ray starts.
bool raycast(const Vec3& origin, const Vec3& direction, float maxDistance,
             const PhysicsWorld& world, RaycastHit& hit);

} // namespace scene
} // namespace s3d
