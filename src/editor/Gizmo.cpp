#include "editor/Gizmo.hpp"

#include "editor/Picking.hpp"

#include <cmath>

namespace s3d {
namespace gizmo {
namespace {

const Vec3 kWorldAxes[3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};

// Screen tolerance for grabbing a handle, relative to its distance from the eye.
constexpr float kRelativePickRadius = 0.10f;

// The rotate rings are sampled at this many steps when picking and drawn with the
// same count, so the grabbable curve matches the visible one exactly.
constexpr int kRingSteps = 48;

// Screen-space slack for grabbing a ring, in pixels. A ring seen edge-on
// collapses to a line, and this is what keeps it catchable when it does.
constexpr float kRingPickRadius = 12.0f;

} // namespace

void axesFor(const RigidBody& body, Mode mode, Vec3 out[3]) {
    for (int i = 0; i < 3; ++i) out[i] = kWorldAxes[i];
    // Scale handles follow the body - scaling in the body's own frame is what you
    // want, and a non-uniform body must be scaled along its own axes.
    //
    // Rotate rings do NOT. The axis a ring turns about has to be the same one
    // before the drag, during it and after it: with local axes the ring swung
    // around with the body, the angle was re-read in a frame that moved under the
    // pointer, and the object turned the wrong way as soon as it was no longer
    // axis-aligned. World axes, like the move handles.
    if (mode == Mode::Scale)
        for (int i = 0; i < 3; ++i) out[i] = rotate(body.orientation, kWorldAxes[i]);
}

float handleLength(const RigidBody& body) { return body.boundRadius * 1.6f + 0.5f; }

float ringRadius(const RigidBody& body) { return body.boundRadius * 1.35f + 0.35f; }

void ringFrame(const Vec3& axis, Vec3& u, Vec3& v) {
    // Same construction as the arrow heads, so a ring's plane is perpendicular to
    // the axis it turns about.
    u = std::fabs(axis.y) < 0.9f ? cross(axis, Vec3{0.0f, 1.0f, 0.0f})
                                  : cross(axis, Vec3{1.0f, 0.0f, 0.0f});
    u = u.normalized();
    v = cross(axis, u);
}

int pickAxis(float pointerX, float pointerY, const Vec3& rayOrigin, const Vec3& rayDir,
             const picking::ViewProjection& projection, const Rect& viewport,
             const RigidBody& body, Mode mode, bool flat) {
    // Rotate has no axis to grab; its handles are rings and pickRing handles those.
    // Silently answering -1 here keeps a caller that forgot to branch from
    // dragging a straight line through the scene.
    if (mode == Mode::Rotate) return -1;

    const float length = handleLength(body);

    Vec3 axes[3];
    axesFor(body, mode, axes);

    int bestAxis = -1;
    float bestScreenDistanceSq = 1e30f;
    // A flat world has two directions to move in: the third handle is neither
    // drawn nor picked, and a handle that is not drawn must not be grabbable.
    const int axisCount = flat ? 2 : 3;
    for (int i = 0; i < axisCount; ++i) {
        float axisT = 0.0f, rayT = 0.0f;
        picking::closestOnAxis(rayOrigin, rayDir, body.position, axes[i], axisT, rayT);
        if (axisT < 0.0f || axisT > length || rayT <= 0.0f) continue;

        // Reject points that are far from the axis line in world space first...
        const Vec3 onAxis = body.position + axes[i] * axisT;
        const Vec3 onRay = rayOrigin + rayDir * rayT;
        if ((onRay - onAxis).length() >= kRelativePickRadius * (onAxis - rayOrigin).length())
            continue;

        // ...then let the closest handle in screen space win.
        float pixelX = 0.0f, pixelY = 0.0f;
        if (!picking::projectToViewport(onAxis, projection, viewport.width, viewport.height,
                                        pixelX, pixelY))
            continue;

        const float dx = pixelX - pointerX;
        const float dy = pixelY - pointerY;
        const float distanceSq = dx * dx + dy * dy;
        if (distanceSq < bestScreenDistanceSq) {
            bestScreenDistanceSq = distanceSq;
            bestAxis = i;
        }
    }
    return bestAxis;
}

int pickRing(float pointerX, float pointerY, const Vec3& rayOrigin, const Vec3& rayDir,
             const picking::ViewProjection& projection, const Rect& viewport,
             const RigidBody& body, bool flat) {
    Vec3 axes[3];
    axesFor(body, Mode::Rotate, axes);
    const float radius = ringRadius(body);

    int bestAxis = -1;
    float bestDistanceSq = kRingPickRadius * kRingPickRadius;
    // A flat world turns in its own plane: the only ring is the one about Z.
    const int first = flat ? 2 : 0;
    for (int i = first; i <= 2; ++i) {
        Vec3 u, v;
        ringFrame(axes[i], u, v);

        for (int step = 0; step < kRingSteps; ++step) {
            const float a = (float)step * (2.0f * 3.14159265f / (float)kRingSteps);
            const Vec3 point = body.position + (u * std::cos(a) + v * std::sin(a)) * radius;

            float pixelX = 0.0f, pixelY = 0.0f;
            if (!picking::projectToViewport(point, projection, viewport.width, viewport.height,
                                            pixelX, pixelY))
                continue;

            const float dx = pixelX - pointerX;
            const float dy = pixelY - pointerY;
            const float distanceSq = dx * dx + dy * dy;
            // Strictly closer than the current best: the first ring to claim the
            // pointer keeps it, which matches the order the rings are drawn in.
            if (distanceSq < bestDistanceSq) {
                bestDistanceSq = distanceSq;
                bestAxis = i;
            }
        }
    }
    return bestAxis;
}

} // namespace gizmo
} // namespace s3d