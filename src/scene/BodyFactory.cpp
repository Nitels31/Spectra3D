#include "scene/BodyFactory.hpp"

#include "core/Log.hpp"
#include "core/Math.hpp"

namespace s3d {
namespace {

// Default size used by the spawn shortcuts, per shape.
Vec3 defaultHalfExtents(Shape shape) {
    switch (shape) {
        case Shape::Sphere: return {0.5f, 0.5f, 0.5f};
        case Shape::Cylinder: return {0.5f, 0.75f, 0.5f};
        case Shape::Capsule: return {0.4f, 0.6f, 0.4f};
        case Shape::Cone: return {0.5f, 0.75f, 0.5f};
        case Shape::Pyramid: return {0.6f, 0.75f, 0.6f};
        case Shape::Prism: return {0.5f, 0.75f, 0.5f};
        // A square in the XY plane. The thickness is the shape's own business.
        case Shape::Quad: return {0.5f, 0.5f, 0.5f};
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

    // The material of the body is what it is made of, and the mass follows
    // from it: the same stuff in a bigger box is heavier. The density is kept
    // on the body as well, so the inspector shows what the thing is made of
    // and not only how much it weighs.
    body.material.density = std::fmax(0.0f, density);
    body.setMass(body.material.density * body.volume());
    // A body the world has never stepped has no "before" state, and the renderer
    // draws lerp(prev, curr, alpha) - so without this the first thing drawn is the
    // body sitting on the world origin.
    body.syncPrevTransform();
    return body;
}

RigidBody BodyFactory::makeInFrontOf(Shape shape, const Camera& camera, float density) {
    Vec3 position;
    if (camera.orthographic) {
        // A flat picture is a picture of a plane, and a thing made in it
        // belongs in that plane. The eye is a picture's worth of distance off
        // it, and "six units in front of the eye" would put the thing in the
        // air: it would be drawn in the right place and it would be nowhere
        // near the ground it stands on.
        const Vec3 forward = camera.forward().normalized();
        position = camera.position - forward * dot(camera.position, forward);
    } else {
        position = camera.position + camera.forward().normalized() * 6.0f;
    }
    position.y = std::round(position.y);
    return make(shape, position, Vec3{}, defaultHalfExtents(shape), density, Vec3{}, Quat{});
}

RigidBody BodyFactory::makeWater(const Vec3& position, float width, float depth,
                                const Vec3& color, float alpha) {
    RigidBody body = make(Shape::Water, position, Vec3{}, Vec3{width * 0.5f, 0.02f, depth * 0.5f},
                           1.0f, Vec3{}, Quat{});
    body.material.color = color;
    body.waterAlpha = clampf(alpha, 0.0f, 1.0f);
    // The colour of the water is the colour of the body, and it is a colour
    // and not a single one: the water is bright where it is thin and dark
    // where it is deep, and the colour of the body is the colour of the
    // shallow water. The deep water is the same colour with the light of it
    // taken away.
    body.water.shallowColour = Vec3{clampf(color.x * 1.7f, 0.0f, 1.0f),
                                    clampf(color.y * 1.7f, 0.0f, 1.0f),
                                    clampf(color.z * 1.7f, 0.0f, 1.0f)};
    body.water.deepColour = Vec3{color.x * 0.3f, color.y * 0.3f, color.z * 0.45f};
    // Water does not fall and it does not drift, and it is not something the
    // simulation has an opinion about.
    body.material.isStatic = true;
    // And it casts no shadow. A flat sheet in the map of the shadows is a flat
    // sheet that shadows itself - every dip of a wave under the plane - and
    // what the water shows of the sun is not a shadow of the water but the
    // track of the sun on it.
    body.castsShadow = false;
    body.configure(1.0f);
    body.syncPrevTransform();
    return body;
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