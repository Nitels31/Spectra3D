#include "scene/Raycast.hpp"

#include <cmath>

namespace s3d {
namespace scene {

bool intersectObb(const Vec3& origin, const Vec3& direction, const RigidBody& body,
                  float& distance, Vec3& normal) {
    const Quat inverse = body.orientation.conjugate();
    const Vec3 localOrigin = rotate(inverse, origin - body.position);
    const Vec3 localDir = rotate(inverse, direction);
    const Vec3& he = body.halfExtents;

    const float originComp[3] = {localOrigin.x, localOrigin.y, localOrigin.z};
    const float dirComp[3] = {localDir.x, localDir.y, localDir.z};
    const float halfComp[3] = {he.x, he.y, he.z};

    float tMin = 0.0f, tMax = 1e30f;
    int axis = 0;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(dirComp[i]) < 1e-9f) {
            // A ray that is parallel to a face either misses it outright or
            // runs along it, and running along it is not a hit.
            if (std::fabs(originComp[i]) > halfComp[i]) return false;
            continue;
        }
        float t1 = (-halfComp[i] - originComp[i]) / dirComp[i];
        float t2 = (halfComp[i] - originComp[i]) / dirComp[i];
        if (t1 > t2) std::swap(t1, t2);
        if (t1 > tMin) {
            tMin = t1;
            axis = i;
        }
        if (t2 < tMax) tMax = t2;
        if (tMin > tMax) return false;
    }

    distance = tMin;

    // The face it met: the axis the ray came in through, pointing out of the
    // body. The face is a direction in the body's own space and is turned
    // with it.
    Vec3 localNormal;
    if (axis == 0)
        localNormal = Vec3{localOrigin.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f};
    else if (axis == 1)
        localNormal = Vec3{0.0f, localOrigin.y >= 0.0f ? 1.0f : -1.0f, 0.0f};
    else
        localNormal = Vec3{0.0f, 0.0f, localOrigin.z >= 0.0f ? 1.0f : -1.0f};
    normal = rotate(body.orientation, localNormal);
    return true;
}

bool intersectSphere(const Vec3& origin, const Vec3& direction, const RigidBody& body,
                     float& distance, Vec3& normal) {
    const float radius = body.halfExtents.x;
    const Vec3 toCenter = origin - body.position;
    const float b = dot(toCenter, direction);
    const float c = dot(toCenter, toCenter) - radius * radius;
    const float discriminant = b * b - c;
    if (discriminant < 0.0f) return false;

    const float root = std::sqrt(discriminant);
    float t = -b - root;
    if (t <= 0.0f) t = -b + root;
    if (t <= 0.0f) return false;

    distance = t;
    normal = (origin + direction * t - body.position).normalized();
    return true;
}

bool intersectBody(const Vec3& origin, const Vec3& direction, const RigidBody& body,
                   float& distance, Vec3& normal) {
    return body.shape == Shape::Sphere
               ? intersectSphere(origin, direction, body, distance, normal)
               : intersectObb(origin, direction, body, distance, normal);
}

bool raycast(const Vec3& origin, const Vec3& direction, float maxDistance,
             const PhysicsWorld& world, RaycastHit& hit) {
    const Vec3 ray = direction.normalized();
    if (ray.lengthSq() < 1e-8f) return false;

    float nearest = maxDistance > 0.0f ? maxDistance : 1e30f;
    bool found = false;

    for (size_t i = 0; i < world.bodies.size(); ++i) {
        const RigidBody& body = world.bodies[i];
        float distance = 0.0f;
        Vec3 normal;
        // A body the ray starts inside is not something that was pointed at:
        // the camera sits in its own body, and a ray from the eye would pick
        // the camera instead of what is in front of it.
        if (!intersectBody(origin, ray, body, distance, normal)) continue;
        if (distance <= 0.0f || distance >= nearest) continue;

        nearest = distance;
        found = true;
        hit.body = (int)i;
        hit.distance = distance;
        hit.point = origin + ray * distance;
        hit.normal = normal;
    }
    return found;
}

} // namespace scene
} // namespace s3d
