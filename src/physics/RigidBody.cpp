#include "RigidBody.hpp"

namespace s3d {

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
    }
    return "?";
}

} // namespace s3d