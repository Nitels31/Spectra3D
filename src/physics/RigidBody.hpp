#pragma once
#include <cstdint>

#include "../core/Math.hpp"
#include "Material.hpp"

namespace s3d {

enum class Shape {
    Box,
    Sphere,
    Cylinder,
    Capsule,
    Cone,
    Pyramid,
    Prism,
    Mesh, // rendered asset, collided as a box hull
};

// Collision primitive of a rigid body plus everything the simulation needs to
// integrate it. Shape dimensions live in dedicated fields; `halfExtents` is
// always the authoritative box hull used by the broadphase and OBB tests.
struct RigidBody {
    // --- state -------------------------------------------------------------
    Vec3 position;
    Vec3 velocity;
    Quat orientation;
    Vec3 angularVelocity;
    Vec3 force;
    Vec3 torque;

    // --- shape -------------------------------------------------------------
    Shape shape = Shape::Box;
    Vec3 halfExtents{0.5f, 0.5f, 0.5f};
    float cylinderRadius = 0.5f;
    float capsuleRadius = 0.3f;
    float capsuleHeight = 1.0f;
    float coneRadius = 0.5f;
    float coneHeight = 1.0f;
    float pyramidBaseRadius = 0.6f;
    float pyramidHeight = 1.0f;
    float prismRadius = 0.5f;
    float prismHeight = 1.0f;
    int prismSides = 5;

    // `mesh` indexes MeshRegistry, which lives one layer up in `assets`. The
    // physics layer therefore only carries the opaque handle, never the type.
    int mesh = -1;
    float meshScale = 1.0f;

    // --- mass properties ---------------------------------------------------
    float mass = 1.0f;
    float invMass = 1.0f;
    Vec3 invInertiaBody{1.0f, 1.0f, 1.0f};
    float boundRadius = 1.0f;

    // --- derived per step --------------------------------------------------
    Mat3 invInertiaCached;
    Vec3 corners[8];
    Vec3 aabbMin;
    Vec3 aabbMax;

    // --- simulation flags --------------------------------------------------
    bool asleep = false;
    float sleepTime = 0.0f;
    bool touching = false;
    bool visible = true;
    bool locked = false;

    // --- soft constraint ---------------------------------------------------
    bool anchorPosition = false;
    bool anchorRotation = false;
    Vec3 anchorPoint;
    Quat anchorOrientation{1.0f, 0.0f, 0.0f, 0.0f};
    float anchorSpring = 50.0f;

    Material material;

    // Warm-start impulses per arena wall and per OBB corner. `warmStamp` marks
    // which entries were refreshed by the current step, so impulses left over
    // from an older contact cannot re-enter after a body leaves the surface.
    float warmNormal[6][8] = {};
    float warmTangent[6][8] = {};
    uint16_t warmStamp[6][8] = {};

    // Recomputes mass properties from the current shape and dimensions and
    // clamps them into valid ranges. Called after every dimension edit.
    void configure(float newMass);

    Mat3 invInertiaWorld() const;
    Vec3 pointVelocity(const Vec3& arm) const;
    void applyImpulse(const Vec3& impulse, const Vec3& arm);
    void wake();

    bool hasMesh() const { return mesh >= 0; }
};

const char* shapeName(Shape s);

} // namespace s3d