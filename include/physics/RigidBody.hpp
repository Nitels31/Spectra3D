#pragma once
#include <cstdint>
#include <string>

#include "core/Math.hpp"
#include "physics/Material.hpp"

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
    // A camera in the scene: a thing that is looked at rather than a thing that
    // is looked through. It is never collided and never simulated.
    //
    // Last in the list on purpose: the ordinal is what a saved scene stores, so
    // a new shape has to be appended or every scene written before it changes
    // what it means.
    Camera,
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

    // The state BEFORE the last integration, for render interpolation. A body
    // drawn at lerp(prevPosition, position, alpha) instead of position moves
    // smoothly between the fixed 60 Hz physics steps instead of jumping. Only
    // the visual read uses these; the simulation never does.
    //
    // They are seeded from the current transform rather than left at zero, because
    // a zero here is not "at the origin": the renderer draws lerp(prev, curr, alpha),
    // so a body that was placed but never integrated reads as sitting on the world
    // origin instead of where it was put. See syncPrevTransform().
    Vec3 prevPosition;
    Quat prevOrientation;

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

    // A camera's field of view in degrees, measured vertically - the same
    // measure the engine's own view camera uses, so the two mean the same
    // thing and a camera entity needs no conversion to be looked through.
    float cameraFov = 70.0f;

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
    // Whether the body casts a shadow. A body that does not is still drawn
    // and still catches a shadow - it is only the shadow it casts that is
    // not there. A flag on the body and not a setting, because which bodies
    // cast a shadow is a question about the scene.
    bool castsShadow = true;
    // Starred in the hierarchy. Purely an editor mark - the simulation never reads
    // it - but it lives here and in the scene file next to `visible` and `locked`
    // because those are editor marks too, and a star that forgets itself on reload
    // is not a star.
    bool favorite = false;

    // The scene's active camera: the one the viewport looks through. Only one
    // body carries it at a time - see PhysicsWorld::setActiveCamera, which is
    // the only thing that should ever set it.
    bool activeCamera = false;

    // The sound this body makes when it hits something. A name, not a file:
    // the physics layer carries the name and the shell that owns the device
    // decides what it means, the same way `mesh` is an index and not a path.
    std::string sound;
    // How loud that sound is, 0..1. The shell scales it by the strength of the
    // hit on top of this.
    float soundVolume = 1.0f;

    // --- soft constraint ---------------------------------------------------
    bool anchorPosition = false;
    bool anchorRotation = false;
    Vec3 anchorPoint;
    Quat anchorOrientation{1.0f, 0.0f, 0.0f, 0.0f};
    float anchorSpring = 50.0f;

    Material material;

    // There is no warm-start cache here. The walls are gone - the world has no
    // box around it - and the impulses of a body against another body live in
    // the physics world's pair table, which outlives the contacts of a step.

    // Recomputes mass properties from the current shape and dimensions and
    // clamps them into valid ranges. Called after every dimension edit.
    void configure(float newMass);

    // Declares the current transform to be the state before the next step, so a
    // body that has not been integrated yet draws where it actually is.
    //
    // PhysicsWorld::step does this for every body it integrates. Everything else
    // that puts a body somewhere - the spawn helpers, the loaders, a gizmo drag -
    // has to do it too, or the renderer interpolates from a transform that has
    // nothing to do with the body and draws it at the world origin.
    void syncPrevTransform();

    Mat3 invInertiaWorld() const;
    Vec3 pointVelocity(const Vec3& arm) const;
    void applyImpulse(const Vec3& impulse, const Vec3& arm);
    void wake();

    bool hasMesh() const { return mesh >= 0; }
    bool isCamera() const { return shape == Shape::Camera; }
};

const char* shapeName(Shape s);

} // namespace s3d