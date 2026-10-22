#include "editor/Picking.hpp"

#include "render/Camera.hpp"
#include "scene/Raycast.hpp"

namespace s3d {
namespace picking {

// The test of a body against a ray lives in the scene layer, because a script
// asks the same question and the engine is what answers it. The editor keeps
// its own name for it, and the answer is the same one.
bool intersectObb(const Vec3& origin, const Vec3& direction, const RigidBody& body,
                  float& hitDistance) {
    Vec3 normal;
    return scene::intersectObb(origin, direction, body, hitDistance, normal);
}

bool intersectSphere(const Vec3& origin, const Vec3& direction, const RigidBody& body,
                     float& hitDistance) {
    Vec3 normal;
    return scene::intersectSphere(origin, direction, body, hitDistance, normal);
}

bool intersectBody(const Vec3& origin, const Vec3& direction, const RigidBody& body,
                   float& hitDistance) {
    Vec3 normal;
    return scene::intersectBody(origin, direction, body, hitDistance, normal);
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

void viewProjection(const Camera& camera, float width, float height, ViewProjection& out) {
    camera.basis(out.forward, out.right, out.up);
    out.position = camera.position;
    out.orthographic = camera.orthographic;

    const float h = height > 1.0f ? height : 1.0f;
    const float w = width > 1.0f ? width : 1.0f;
    if (camera.orthographic) {
        // The picture is a box: the same size of the world on every side, and
        // the width follows from the aspect of the viewport.
        out.halfHeight = camera.orthoSize > 0.0f ? camera.orthoSize : 1.0f;
        out.halfWidth = out.halfHeight * (w / h);
        out.tanX = 0.0f;
        out.tanY = 0.0f;
        return;
    }
    // The camera's field of view is over the viewport's height, and the
    // horizontal one follows from the aspect. The renderer projects with the
    // same two numbers, and they are here rather than in the editor so a test
    // can compare the two.
    const float tanHalf = std::tan(camera.fovDeg * kPi / 360.0f);
    out.tanY = tanHalf;
    out.tanX = tanHalf * (w / h);
    out.halfWidth = 0.0f;
    out.halfHeight = 0.0f;
}

bool projectToViewport(const Vec3& worldPoint, const ViewProjection& projection, float width,
                       float height, float& outX, float& outY) {
    const Vec3 relative = worldPoint - projection.position;
    if (projection.orthographic) {
        // No depth in the answer: the picture is the world measured, and a
        // thing at the back is the same size as a thing at the front.
        if (projection.halfWidth <= 0.0f || projection.halfHeight <= 0.0f) return false;
        outX = (dot(relative, projection.right) / projection.halfWidth * 0.5f + 0.5f) * width;
        outY = (0.5f - dot(relative, projection.up) / projection.halfHeight * 0.5f) * height;
        return true;
    }
    const float depth = dot(relative, projection.forward);
    if (depth <= 1e-4f) return false;
    outX = ((dot(relative, projection.right) / (depth * projection.tanX)) * 0.5f + 0.5f) * width;
    outY = (0.5f - (dot(relative, projection.up) / (depth * projection.tanY)) * 0.5f) * height;
    return true;
}

void rayFromViewport(float pixelX, float pixelY, const Rect& viewport,
                     const ViewProjection& projection, Vec3& originOut, Vec3& directionOut) {
    const float width = viewport.width > 1.0f ? viewport.width : 1.0f;
    const float height = viewport.height > 1.0f ? viewport.height : 1.0f;
    const float localX = pixelX - viewport.x;
    const float localY = pixelY - viewport.y;
    // The viewport's own space: the middle of it is the camera's forward, and
    // the edges are the camera's field of view. The same two numbers the
    // projection is built from, so the pointer and the picture agree about
    // where a thing is.
    const float ndcX = 2.0f * localX / width - 1.0f;
    const float ndcY = 1.0f - 2.0f * localY / height;
    if (projection.orthographic) {
        // The rays of an orthographic camera are parallel: every one of them
        // goes the same way, and where the pointer is says which of them it
        // is. The ray starts at the point of the picture the pointer is over -
        // the plane the eye looks at, at the pointer's own place in it.
        //
        // A ray that started at the eye instead would be the same ray for
        // every pixel of the picture: it would pick whatever is under the
        // middle of the screen, and a click on empty space would take a body
        // with it.
        directionOut = projection.forward;
        originOut = projection.position + projection.right * (ndcX * projection.halfWidth) +
                    projection.up * (ndcY * projection.halfHeight);
        return;
    }
    directionOut = (projection.forward + projection.right * (ndcX * projection.tanX) +
                    projection.up * (ndcY * projection.tanY))
                       .normalized();
    // Every ray of a perspective camera starts at the eye.
    originOut = projection.position;
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