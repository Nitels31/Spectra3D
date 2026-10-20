#include "editor/Picking.hpp"

#include "render/Camera.hpp"

namespace s3d {
namespace picking {

bool intersectObb(const Vec3& origin, const Vec3& direction, const RigidBody& body,
                  float& hitDistance) {
    const Quat inverse = body.orientation.conjugate();
    const Vec3 localOrigin = rotate(inverse, origin - body.position);
    const Vec3 localDir = rotate(inverse, direction);
    const Vec3& he = body.halfExtents;

    const float originComp[3] = {localOrigin.x, localOrigin.y, localOrigin.z};
    const float dirComp[3] = {localDir.x, localDir.y, localDir.z};
    const float halfComp[3] = {he.x, he.y, he.z};

    float tMin = 0.0f, tMax = 1e30f;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(dirComp[i]) < 1e-9f) {
            if (std::fabs(originComp[i]) > halfComp[i]) return false;
            continue;
        }
        float t1 = (-halfComp[i] - originComp[i]) / dirComp[i];
        float t2 = (halfComp[i] - originComp[i]) / dirComp[i];
        if (t1 > t2) std::swap(t1, t2);
        tMin = t1 > tMin ? t1 : tMin;
        tMax = t2 < tMax ? t2 : tMax;
        if (tMin > tMax) return false;
    }
    hitDistance = tMin;
    return true;
}

bool intersectSphere(const Vec3& origin, const Vec3& direction, const RigidBody& body,
                     float& hitDistance) {
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
    hitDistance = t;
    return true;
}

bool intersectBody(const Vec3& origin, const Vec3& direction, const RigidBody& body,
                   float& hitDistance) {
    return body.shape == Shape::Sphere ? intersectSphere(origin, direction, body, hitDistance)
                                       : intersectObb(origin, direction, body, hitDistance);
}

int pickBody(const Vec3& origin, const Vec3& direction, const PhysicsWorld& world) {
    int hit = -1;
    float nearest = 1e30f;
    for (const RigidBody& body : world.bodies) {
        float distance = 0.0f;
        // A body the ray starts inside is not something that was clicked: the
        // camera sits in its own body, and a click anywhere in the scene would
        // pick the camera instead of what is under the pointer.
        if (intersectBody(origin, direction, body, distance) && distance > 0.0f &&
            distance < nearest) {
            nearest = distance;
            hit = (int)(&body - world.bodies.data());
        }
    }
    return hit;
}

void closestOnAxis(const Vec3& origin, const Vec3& direction, const Vec3& p0,
                   const Vec3& axis, float& t, float& s) {
    const float c = dot(direction, axis);
    const float denominator = 1.0f - c * c;
    if (denominator < 1e-6f) {
        t = 0.0f;
        s = 0.0f;
        return;
    }
    const Vec3 w = origin - p0;
    s = (c * dot(w, axis) - dot(w, direction)) / denominator;
    t = dot(w, axis) + s * c;
}

bool projectToViewport(const Vec3& worldPoint, const Vec3& cameraPos, const Vec3& forward,
                       const Vec3& right, const Vec3& up, float tanX, float tanY,
                       float width, float height, float& outX, float& outY) {
    const Vec3 relative = worldPoint - cameraPos;
    const float depth = dot(relative, forward);
    if (depth <= 1e-4f) return false;
    outX = ((dot(relative, right) / (depth * tanX)) * 0.5f + 0.5f) * width;
    outY = (0.5f - (dot(relative, up) / (depth * tanY)) * 0.5f) * height;
    return true;
}

Vec3 rayFromViewport(float pixelX, float pixelY, const Rect& viewport, const Vec3& forward,
                     const Vec3& right, const Vec3& up, float tanX, float tanY) {
    const float localX = pixelX - viewport.x;
    const float localY = pixelY - viewport.y;
    // The viewport's own space: the middle of it is the camera's forward, and
    // the edges are the camera's field of view. The same two numbers the
    // projection is built from, so the pointer and the picture agree about
    // where a thing is.
    const float ndcX = 2.0f * localX / viewport.width - 1.0f;
    const float ndcY = 1.0f - 2.0f * localY / viewport.height;
    return (forward + right * (ndcX * tanX) + up * (ndcY * tanY)).normalized();
}

void viewRayTangents(const Camera& camera, float width, float height, float& tanX, float& tanY) {
    // The camera's field of view is over the viewport's height, and the
    // horizontal one follows from the aspect. The renderer projects with the
    // same two numbers, and they are here rather than in the editor so a test
    // can compare the two.
    const float tanHalf = std::tan(camera.fovDeg * kPi / 360.0f);
    const float h = height > 1.0f ? height : 1.0f;
    const float w = width > 1.0f ? width : 1.0f;
    tanY = tanHalf;
    tanX = tanHalf * (w / h);
}

} // namespace picking
} // namespace s3d