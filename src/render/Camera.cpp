#include "render/Camera.hpp"

namespace s3d {

void Camera::refreshBasis() const {
    if (basisValid_ && basisYaw_ == yaw && basisPitch_ == pitch) return;

    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    basisForward_ = Vec3{sy * cp, sp, -cy * cp};
    basisRight_ = Vec3{cy, 0.0f, sy};
    basisUp_ = cross(basisRight_, basisForward_);

    basisYaw_ = yaw;
    basisPitch_ = pitch;
    basisValid_ = true;
}

Vec3 Camera::forward() const {
    refreshBasis();
    return basisForward_;
}

Vec3 Camera::right() const {
    refreshBasis();
    return basisRight_;
}

Vec3 Camera::up() const {
    refreshBasis();
    return basisUp_;
}

void Camera::basis(Vec3& forwardOut, Vec3& rightOut, Vec3& upOut) const {
    refreshBasis();
    forwardOut = basisForward_;
    rightOut = basisRight_;
    upOut = basisUp_;
}

void Camera::rotate(float dYaw, float dPitch) {
    yaw += dYaw;
    pitch = clampf(pitch - dPitch, -1.55f, 1.55f);
}

void Camera::translate(const Vec3& worldDirection, float speed, float dt) {
    if (worldDirection.lengthSq() <= 0.0f) return;
    position += worldDirection.normalized() * (speed * dt);
}

} // namespace s3d