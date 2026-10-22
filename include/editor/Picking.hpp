#pragma once
#include "core/Math.hpp"
#include "physics/PhysicsWorld.hpp"
#include "render/Camera.hpp"

namespace s3d {
namespace picking {

// HOW THE CAMERA PROJECTS, IN ONE PLACE.
//
// A perspective camera and an orthographic one need different numbers, and
// everything that turns a pixel into a ray or a world point into a pixel has
// to know which one it is looking through. Both sets of numbers are here so
// that no caller has to ask again, and so that the picture and the pointer
// cannot disagree: they are made from the same camera and the same viewport.
struct ViewProjection {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 forward{0.0f, 0.0f, -1.0f};
    Vec3 right{1.0f, 0.0f, 0.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    // Perspective: the tangents of the half-angles of the field of view.
    float tanX = 0.0f, tanY = 0.0f;
    // Orthographic: the half-extents of the picture, in world units.
    float halfWidth = 0.0f, halfHeight = 0.0f;
    bool orthographic = false;
    // True once the numbers have been worked out. The editor asks for them at
    // most once per frame, and a frame where the pointer never reaches the
    // viewport never pays for them.
    bool valid = false;
};

// Fills the projection of `camera` over a viewport of this size.
void viewProjection(const Camera& camera, float width, float height, ViewProjection& out);

// Projects a world point into viewport pixel coordinates. Returns false when
// the point is behind the camera.
bool projectToViewport(const Vec3& worldPoint, const ViewProjection& projection, float width,
                       float height, float& outX, float& outY);

// Builds a world-space ray from a pixel inside the viewport. The middle of
// the viewport is the camera's forward and its edges are the camera's field
// of view, so the ray and the picture are the same thing seen two ways.
Vec3 rayFromViewport(float pixelX, float pixelY, const Rect& viewport,
                      const ViewProjection& projection);

// Slab test against the body's oriented box. Writes the entry distance and
// returns false when the ray misses.
bool intersectObb(const Vec3& origin, const Vec3& direction, const RigidBody& body,
                  float& hitDistance);

// Ray vs sphere centred at the body position.
bool intersectSphere(const Vec3& origin, const Vec3& direction, const RigidBody& body,
                     float& hitDistance);

// Shape dispatch over the two primitives above.
bool intersectBody(const Vec3& origin, const Vec3& direction, const RigidBody& body,
                   float& hitDistance);

// Nearest body hit along the ray, or -1.
int pickBody(const Vec3& origin, const Vec3& direction, const PhysicsWorld& world);

// Closest point approach between a ray and an infinite axis through `p0`.
// Writes the axis parameter in `t` and the ray parameter in `s`.
void closestOnAxis(const Vec3& origin, const Vec3& direction, const Vec3& p0,
                   const Vec3& axis, float& t, float& s);

// Projects a world point into viewport pixel coordinates. Returns false when the
// point is behind the camera.
bool projectToViewport(const Vec3& worldPoint, const Vec3& cameraPos, const Vec3& forward,
                       const Vec3& right, const Vec3& up, float tanX, float tanY,
                       float width, float height, float& outX, float& outY);

// Builds a world-space ray from a pixel inside the viewport. The middle of the
// viewport is the camera's forward and its edges are the camera's field of
// view, so the ray and the picture are the same thing seen two ways.
Vec3 rayFromViewport(float pixelX, float pixelY, const Rect& viewport, const Vec3& forward,
                     const Vec3& right, const Vec3& up, float tanX, float tanY);

// The tangents of the camera's field of view over a viewport of this size: the
// vertical one over the height, the horizontal one from the aspect. The same
// numbers the renderer projects with, so the pointer and the picture cannot
// disagree about where a thing is.
void viewRayTangents(const Camera& camera, float width, float height, float& tanX, float& tanY);

} // namespace picking
} // namespace s3d