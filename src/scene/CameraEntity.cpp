#include "scene/CameraEntity.hpp"

#include <cmath>

namespace s3d {
namespace {

// The angle below which a camera is treated as looking straight up or down.
//
// A camera that looks along the pole has no yaw: the forward vector's x and z
// are both zero, and atan2 of that is a number that depends on how the vector
// was rounded rather than on where the camera points. The pitch still carries
// the direction, so the yaw is left alone instead of being invented.
constexpr float kPoleEpsilon = 0.9999f;

} // namespace

Vec3 forwardFromEntity(const RigidBody& body) {
    // A camera looks down its local -Z, the same way the view camera looks at
    // yaw 0. Every other convention would put the entity's nose somewhere the
    // engine never expects a camera to point.
    return rotate(body.orientation, Vec3{0.0f, 0.0f, -1.0f});
}

Quat orientationFromCamera(const Camera& camera) {
    // The view camera's forward is {sin(yaw)cos(pitch), sin(pitch),
    // -cos(yaw)cos(pitch)}, which is Ry(-yaw) applied to Rx(pitch) applied to
    // (0,0,-1). The yaw is negated because the engine's yaw turns the other way
    // round from a right-handed rotation about Y - the formula above is what
    // decides that, and this is the quaternion that produces it.
    const Quat yaw = Quat::axisAngle(Vec3{0.0f, 1.0f, 0.0f}, -camera.yaw);
    const Quat pitch = Quat::axisAngle(Vec3{1.0f, 0.0f, 0.0f}, camera.pitch);
    return (yaw * pitch).normalized();
}

Camera cameraFromEntity(const RigidBody& body) {
    Camera camera;
    camera.position = body.position;

    const Vec3 forward = forwardFromEntity(body);
    camera.pitch = std::asin(clampf(forward.y, -1.0f, 1.0f));
    if (std::fabs(forward.y) < kPoleEpsilon)
        camera.yaw = std::atan2(forward.x, -forward.z);

    camera.fovDeg = body.cameraFov;
    return camera;
}

} // namespace s3d
