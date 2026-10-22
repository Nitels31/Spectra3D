#include "physics/RigidBody.hpp"

#include <cmath>

namespace s3d {
namespace {

// Four thirds of pi: the volume of a sphere of a radius of one. The pi is the
// one of core/Math.hpp - a second one here would be a second name for the same
// number in the same namespace, and the compiler cannot tell which one a line
// means.
constexpr float kSphereVolume = 4.0f / 3.0f * kPi;

// The area of a regular polygon with `sides` sides, drawn around a circle of
// the given radius.
//
// A square of that radius has an area of twice the radius squared, and that
// is where the pyramid and the prism get their volume from.
float polygonArea(int sides, float radius) {
    if (sides < 3) sides = 3;
    return 0.5f * (float)sides * radius * radius *
           std::sin(2.0f * kPi / (float)sides);
}

} // namespace

float RigidBody::volume() const {
    switch (shape) {
        case Shape::Sphere: {
            const float r = halfExtents.x;
            return kSphereVolume * r * r * r;
        }
        case Shape::Cylinder: {
            const float r = cylinderRadius;
            const float h = halfExtents.y * 2.0f;
            return kPi * r * r * h;
        }
        case Shape::Capsule: {
            const float r = capsuleRadius;
            const float h = capsuleHeight;
            return kPi * r * r * h + kSphereVolume * r * r * r;
        }
        case Shape::Cone: {
            const float r = coneRadius;
            const float h = coneHeight;
            return kPi * r * r * h / 3.0f;
        }
        case Shape::Pyramid: {
            // A pyramid is a third of the prism of the same base: the volume
            // of a pyramid is a third of the volume of the prism that stands
            // on the same base.
            const float r = pyramidBaseRadius;
            const float h = pyramidHeight;
            return polygonArea(4, r) * h / 3.0f;
        }
        case Shape::Prism: {
            // Drawn as a pyramid, and measured as one: the shape the renderer
            // puts on the screen is the shape the weight belongs to.
            const float r = prismRadius;
            const float h = prismHeight;
            return polygonArea(prismSides, r) * h / 3.0f;
        }
        case Shape::Quad: {
            // A flat square: the thickness is small and fixed, and the volume
            // of it is small and fixed too.
            return 8.0f * halfExtents.x * halfExtents.y * halfExtents.z;
        }
        case Shape::Water: {
            // A level of water: the ground it covers, and a metre of it. The
            // depth of the water is a setting of the water and not of the
            // body, and a body of water is never weighed anyway.
            return 4.0f * halfExtents.x * halfExtents.z;
        }
        case Shape::Camera: {
            // A camera is not a thing in the world and it has no volume.
            return 0.0f;
        }
        case Shape::Mesh:
        case Shape::Box:
        default: {
            return 8.0f * halfExtents.x * halfExtents.y * halfExtents.z;
        }
    }
}

void RigidBody::setMass(float newMass) {
    configure(newMass);
    // The density follows the mass, so that the two never say different things
    // about how heavy this is. A body whose mass is a hundred kilograms and
    // whose density says it is made of foam is a body that weighs nothing.
    const float v = volume();
    if (v > 1e-6f) material.density = mass / v;
}

void RigidBody::setDensity(float newDensity) {
    material.density = std::fmax(0.0f, newDensity);
    // The mass follows the density: the same stuff in a bigger box is heavier,
    // and that is what the volume of the box is for.
    const float v = volume();
    if (v > 1e-6f) configure(material.density * v);
}

CustomParam* RigidBody::findParam(const std::string& name) {
    for (CustomParam& param : params)
        if (param.name == name) return &param;
    return nullptr;
}

const CustomParam* RigidBody::findParam(const std::string& name) const {
    for (const CustomParam& param : params)
        if (param.name == name) return &param;
    return nullptr;
}

float RigidBody::paramNumber(const std::string& name) const {
    const CustomParam* param = findParam(name);
    return param ? param->asNumber() : 0.0f;
}

void RigidBody::setParamNumber(const std::string& name, float value) {
    CustomParam* param = findParam(name);
    if (!param) {
        // A parameter that is not there is made, because a script that sets a
        // parameter has said that the thing has one.
        params.push_back(CustomParam{});
        param = &params.back();
        param->name = name;
    }
    switch (param->type) {
        case ParamType::Number:
            param->number = value;
            break;
        case ParamType::Integer:
            param->number = std::round(value);
            break;
        case ParamType::Flag:
            param->flag = value != 0.0f;
            break;
        case ParamType::Text:
            // A word is not a number, and a number is not a word. Setting a
            // word to a number leaves the word alone.
            break;
    }
}

void RigidBody::configure(float newMass) {
    mass = newMass > 1e-6f ? newMass : 1e-6f;
    invMass = 1.0f / mass;

    auto setInertia = [&](float ix, float iy, float iz) {
        invInertiaBody = {1.0f / std::fmax(ix, 1e-6f),
                          1.0f / std::fmax(iy, 1e-6f),
                          1.0f / std::fmax(iz, 1e-6f)};
    };

    switch (shape) {
        case Shape::Sphere: {
            const float r = clampf(halfExtents.x, 0.05f, 10.0f);
            halfExtents = {r, r, r};
            const float i = 0.4f * mass * r * r;
            setInertia(i, i, i);
            boundRadius = r;
            return;
        }
        case Shape::Cylinder: {
            const float r = clampf(cylinderRadius, 0.05f, 10.0f);
            const float h = clampf(halfExtents.y * 2.0f, 0.1f, 20.0f);
            cylinderRadius = r;
            halfExtents = {r, h * 0.5f, r};
            const float ix = mass * (3.0f * r * r + h * h) / 12.0f;
            const float iy = mass * r * r * 0.5f;
            setInertia(ix, iy, ix);
            boundRadius = std::sqrt(r * r + halfExtents.y * halfExtents.y);
            return;
        }
        case Shape::Capsule: {
            const float r = clampf(capsuleRadius, 0.05f, 5.0f);
            const float h = clampf(capsuleHeight, 0.1f, 20.0f);
            capsuleRadius = r;
            capsuleHeight = h;
            halfExtents = {r, h * 0.5f + r, r};
            const float cylMass = mass * h / (h + 4.0f * r / 3.0f);
            const float capMass = mass - cylMass;
            const float icos = cylMass * (3.0f * r * r + h * h) / 12.0f;
            const float isph = 0.4f * capMass * r * r;
            const float shift = (h * 0.5f + r * 0.5f);
            const float i = icos + isph + capMass * shift * shift * 0.3f;
            setInertia(i, i, i);
            boundRadius = halfExtents.y;
            return;
        }
        case Shape::Cone: {
            const float r = clampf(coneRadius, 0.05f, 10.0f);
            const float h = clampf(coneHeight, 0.1f, 20.0f);
            coneRadius = r;
            coneHeight = h;
            halfExtents = {r, h * 0.5f, r};
            const float ix = mass * (3.0f * r * r + h * h * 0.25f) / 20.0f;
            const float iy = mass * r * r * 0.1f;
            setInertia(ix, iy, ix);
            boundRadius = std::sqrt(r * r + halfExtents.y * halfExtents.y);
            return;
        }
        case Shape::Pyramid: {
            const float r = clampf(pyramidBaseRadius, 0.05f, 10.0f);
            const float h = clampf(pyramidHeight, 0.1f, 20.0f);
            pyramidBaseRadius = r;
            pyramidHeight = h;
            halfExtents = {r, h * 0.5f, r};
            const float ib = mass * (r * r * 0.5f + h * h * 0.05f);
            setInertia(ib, ib * 0.15f, ib);
            boundRadius = std::sqrt(r * r + halfExtents.y * halfExtents.y);
            return;
        }
        case Shape::Prism: {
            const float r = clampf(prismRadius, 0.05f, 10.0f);
            const float h = clampf(prismHeight, 0.1f, 20.0f);
            prismRadius = r;
            prismHeight = h;
            prismSides = std::max(3, std::min(12, prismSides));
            halfExtents = {r, h * 0.5f, r};
            const float ix = mass * (r * r * 0.5f + h * h / 12.0f);
            const float iy = mass * r * r * 0.5f;
            setInertia(ix, iy, ix);
            boundRadius = std::sqrt(r * r + halfExtents.y * halfExtents.y);
            return;
        }
        case Shape::Camera: {
            // A camera is a scene object, not a thing in the world. It has no
            // mass to speak of and no inertia to speak of either, and the
            // inverse mass is zero so that nothing can ever push it around.
            //
            // The size is the gizmo's, not a collision hull: a camera is never
            // collided, and the box it is picked by is this one.
            const float r = 0.12f;
            halfExtents = {r, r, r};
            invMass = 0.0f;
            setInertia(1.0f, 1.0f, 1.0f);
            boundRadius = r;
            return;
        }
        case Shape::Quad: {
            // A flat square in the XY plane. The thickness is fixed: a sprite
            // in a flat world is a rectangle, and how thick the rectangle is
            // not is a question nobody asks.
            const float w = clampf(halfExtents.x, 0.01f, 10.0f);
            const float h = clampf(halfExtents.y, 0.01f, 10.0f);
            const float d = 0.02f;
            halfExtents = {w, h, d};
            setInertia(mass / 12.0f * (4.0f * h * h + 4.0f * d * d),
                       mass / 12.0f * (4.0f * w * w + 4.0f * d * d),
                       mass / 12.0f * (4.0f * w * w + 4.0f * h * h));
            boundRadius = std::sqrt(w * w + h * h);
            return;
        }
        case Shape::Water: {
            // A level of water: a flat sheet in the XZ plane. The thickness is
            // fixed for the same reason the quad's is - it is a surface and
            // not a thing, and how thick a surface is nobody asks.
            const float w = clampf(halfExtents.x, 0.1f, 10000.0f);
            const float d = clampf(halfExtents.z, 0.1f, 10000.0f);
            const float t = 0.02f;
            halfExtents = {w, t, d};
            setInertia(mass / 12.0f * (4.0f * d * d + 4.0f * t * t),
                       mass / 12.0f * (4.0f * w * w + 4.0f * d * d),
                       mass / 12.0f * (4.0f * w * w + 4.0f * t * t));
            boundRadius = std::sqrt(w * w + d * d);
            return;
        }
        default: break;
    }

    const Vec3 he{std::fmax(halfExtents.x, 0.01f),
                  std::fmax(halfExtents.y, 0.01f),
                  std::fmax(halfExtents.z, 0.01f)};
    halfExtents = he;
    setInertia(mass / 12.0f * (4.0f * he.y * he.y + 4.0f * he.z * he.z),
               mass / 12.0f * (4.0f * he.x * he.x + 4.0f * he.z * he.z),
               mass / 12.0f * (4.0f * he.x * he.x + 4.0f * he.y * he.y));
    boundRadius = he.length();
}

Mat3 RigidBody::invInertiaWorld() const {
    const Mat3 r = Mat3::fromQuat(orientation);
    const Mat3 d = Mat3::diag(invInertiaBody.x, invInertiaBody.y, invInertiaBody.z);
    return (r * d) * r.transposed();
}

Vec3 RigidBody::pointVelocity(const Vec3& arm) const {
    return velocity + cross(angularVelocity, arm);
}

void RigidBody::applyImpulse(const Vec3& impulse, const Vec3& arm) {
    velocity += impulse * invMass;
    angularVelocity += invInertiaCached * cross(arm, impulse);
}

void RigidBody::wake() {
    asleep = false;
    sleepTime = 0.0f;
}

void RigidBody::syncPrevTransform() {
    prevPosition = position;
    prevOrientation = orientation;
}

const char* shapeName(Shape s) {
    switch (s) {
        case Shape::Box: return "Box";
        case Shape::Sphere: return "Sphere";
        case Shape::Cylinder: return "Cylinder";
        case Shape::Capsule: return "Capsule";
        case Shape::Cone: return "Cone";
        case Shape::Pyramid: return "Pyramid";
        case Shape::Prism: return "Prism";
        case Shape::Mesh: return "Mesh";
        case Shape::Camera: return "Camera";
        case Shape::Quad: return "Quad";
        case Shape::Water: return "Water";
    }
    return "?";
}

} // namespace s3d