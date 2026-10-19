#include "scene/BodyFactory.hpp"

#include "core/Log.hpp"

namespace s3d {
namespace {

constexpr float kPiDensity = 3.14159f;
constexpr float kSphereDensity = 4.18879f; // 4/3 pi

// Volume of each shape for the given dimension triple, used to turn a material
// density into a mass.
float massFor(Shape shape, float density, const Vec3& he) {
    switch (shape) {
        case Shape::Sphere:
            return density * kSphereDensity * he.x * he.x * he.x;
        case Shape::Cylinder:
            return density * kPiDensity * he.x * he.x * he.y * 2.0f;
        case Shape::Capsule:
            return density * (kPiDensity * he.x * he.x * he.y +
                              kSphereDensity * he.x * he.x * he.x);
        case Shape::Cone:
            return density * kPiDensity * he.x * he.x * he.y * 2.0f / 3.0f;
        case Shape::Pyramid:
            return density * he.x * he.x * he.y * 2.0f / 3.0f;
        case Shape::Prism:
            return density * kPiDensity * he.x * he.x * he.y;
        default:
            return density * 8.0f * he.x * he.y * he.z;
    }
}

// Default size used by the spawn shortcuts, per shape.
Vec3 defaultHalfExtents(Shape shape) {
    switch (shape) {
        case Shape::Sphere: return {0.5f, 0.5f, 0.5f};
        case Shape::Cylinder: return {0.5f, 0.75f, 0.5f};
        case Shape::Capsule: return {0.4f, 0.6f, 0.4f};
        case Shape::Cone: return {0.5f, 0.75f, 0.5f};
        case Shape::Pyramid: return {0.6f, 0.75f, 0.6f};
        case Shape::Prism: return {0.5f, 0.75f, 0.5f};
        default: return {1.0f, 1.0f, 1.0f};
    }
}

} // namespace

RigidBody BodyFactory::make(Shape shape, const Vec3& position, const Vec3& velocity,
                            const Vec3& halfExtents, float density,
                            const Vec3& angularVelocity, const Quat& orientation) {
    RigidBody body;
    body.shape = shape;
    body.position = position;
    body.velocity = velocity;
    body.angularVelocity = angularVelocity;
    body.orientation = orientation.normalized();
    body.halfExtents = halfExtents;

    switch (shape) {
        case Shape::Cylinder:
            body.cylinderRadius = halfExtents.x;
            body.halfExtents.y = halfExtents.y;
            break;
        case Shape::Capsule:
            body.capsuleRadius = halfExtents.x;
            body.capsuleHeight = halfExtents.y;
            break;
        case Shape::Cone:
            body.coneRadius = halfExtents.x;
            body.coneHeight = halfExtents.y * 2.0f;
            break;
        case Shape::Pyramid:
            body.pyramidBaseRadius = halfExtents.x;
            body.pyramidHeight = halfExtents.y * 2.0f;
            break;
        case Shape::Prism:
            body.prismRadius = halfExtents.x;
            body.prismHeight = halfExtents.y * 2.0f;
            body.prismSides = 5;
            break;
        default: break;
    }

    body.configure(massFor(shape, density, halfExtents));
    // A body the world has never stepped has no "before" state, and the renderer
    // draws lerp(prev, curr, alpha) - so without this the first thing drawn is the
    // body sitting on the world origin.
    body.syncPrevTransform();
    return body;
}

RigidBody BodyFactory::makeInFrontOf(Shape shape, const Camera& camera, float density) {
    Vec3 position = camera.position + camera.forward().normalized() * 6.0f;
    position.y = std::round(position.y);
    return make(shape, position, Vec3{}, defaultHalfExtents(shape), density, Vec3{}, Quat{});
}

int BodyFactory::spawn(PhysicsWorld& world, Shape shape, const Vec3& position,
                       const Vec3& velocity, const Vec3& halfExtents, float density,
                       const Vec3& angularVelocity, const Quat& orientation) {
    world.bodies.push_back(make(shape, position, velocity, halfExtents, density,
                                angularVelocity, orientation));
    return (int)world.bodies.size() - 1;
}

int BodyFactory::spawnInFrontOf(PhysicsWorld& world, Shape shape, const Camera& camera,
                                float density) {
    const int index = (int)world.bodies.size();
    world.bodies.push_back(makeInFrontOf(shape, camera, density));
    Log::info("Spawned %s (%d)", shapeName(shape), index);
    return index;
}

} // namespace s3d