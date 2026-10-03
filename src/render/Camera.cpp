#include "Camera.hpp"

namespace s3d {

Vec3 Camera::forward() const {
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    return {sy * cp, sp, -cy * cp};
}

Vec3 Camera::right() const {
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    return {cy, 0.0f, sy};
}

Vec3 Camera::up() const { return cross(right(), forward()); }

void Camera::basis(Vec3& forwardOut, Vec3& rightOut, Vec3& upOut) const {
    forwardOut = forward();
    rightOut = right();
    upOut = cross(rightOut, forwardOut);
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