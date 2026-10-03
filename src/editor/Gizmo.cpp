#include "Gizmo.hpp"

#include "Picking.hpp"

#include <cmath>

namespace s3d {
namespace gizmo {
namespace {

const Vec3 kWorldAxes[3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};

// Screen tolerance for grabbing a handle, relative to its distance from the eye.
constexpr float kRelativePickRadius = 0.10f;

} // namespace

void axesFor(const RigidBody& body, Mode mode, Vec3 out[3]) {
    for (int i = 0; i < 3; ++i) out[i] = kWorldAxes[i];
    if (mode == Mode::Scale)
        for (int i = 0; i < 3; ++i) out[i] = rotate(body.orientation, kWorldAxes[i]);
}

float handleLength(const RigidBody& body) { return body.boundRadius * 1.6f + 0.5f; }

int pickAxis(float pointerX, float pointerY, const Vec3& rayOrigin, const Vec3& rayDir,
             const Camera& camera, const Rect& viewport, const RigidBody& body, Mode mode) {
    Vec3 forward, right, up;
    camera.basis(forward, right, up);
    const float tanY = std::tan(camera.fovDeg * kPi / 360.0f);
    const float tanX = tanY * viewport.width / viewport.height;
    const float length = handleLength(body);

    Vec3 axes[3];
    axesFor(body, mode, axes);

    int bestAxis = -1;
    float bestScreenDistanceSq = 1e30f;
    for (int i = 0; i < 3; ++i) {
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
        if (!picking::projectToViewport(onAxis, camera.position, forward, right, up, tanX,
                                        tanY, viewport.width, viewport.height, pixelX,
                                        pixelY))
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

} // namespace gizmo
} // namespace s3d