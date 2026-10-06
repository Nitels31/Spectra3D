#pragma once
#include "physics/PhysicsWorld.hpp"
#include "render/Camera.hpp"

namespace s3d {

// Body construction. Every spawn path goes through here so mass, density and
// dimension clamping stay consistent between the UI, keyboard shortcuts and
// scene loading.
class BodyFactory {
public:
    // Generic spawn: `halfExtents` doubles as the dimension input per shape and
    // the shape's own fields are derived from it.
    static RigidBody make(Shape shape, const Vec3& position, const Vec3& velocity,
                          const Vec3& halfExtents, float density, const Vec3& angularVelocity,
                          const Quat& orientation);

    // Spawns `shape` in front of `camera` with a sane default size.
    static RigidBody makeInFrontOf(Shape shape, const Camera& camera, float density);

    static int spawn(PhysicsWorld& world, Shape shape, const Vec3& position,
                     const Vec3& velocity, const Vec3& halfExtents, float density,
                     const Vec3& angularVelocity, const Quat& orientation);

    static int spawnInFrontOf(PhysicsWorld& world, Shape shape, const Camera& camera,
                              float density);
};

} // namespace s3d