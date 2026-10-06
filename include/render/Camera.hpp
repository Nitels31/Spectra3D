#pragma once
#include "core/Math.hpp"

namespace s3d {

// Yaw/pitch orbit camera used by the editor viewport and the debug fly mode.
struct Camera {
    Vec3 position{0.0f, 6.0f, 34.0f};
    float yaw = 0.0f;
    float pitch = -0.06f;
    float fovDeg = 70.0f;

    Vec3 forward() const;
    Vec3 right() const;
    Vec3 up() const;

    // View basis used for ray casting from the viewport.
    void basis(Vec3& forwardOut, Vec3& rightOut, Vec3& upOut) const;

    void rotate(float dYaw, float dPitch);

    // `worldDirection` is expected in world space and gets normalised, so
    // callers compose it from basis() plus world axes.
    void translate(const Vec3& worldDirection, float speed, float dt);

    // forward/right/up depend on yaw and pitch and on nothing else - not on
    // `position`, not on `fovDeg`. Callers mutate `yaw`/`pitch` directly (orbit
    // gizmo, viewport drag), so the basis cache is keyed on exactly those two
    // floats and revalidated on every read, which makes staleness impossible.
    void refreshBasis() const;

    mutable Vec3 basisForward_{}, basisRight_{}, basisUp_{};
    mutable float basisYaw_ = 0.0f;
    mutable float basisPitch_ = 0.0f;
    mutable bool basisValid_ = false;
};

// Axis aligned rectangle in pixels, origin top-left.
struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    bool contains(float px, float py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
};

} // namespace s3d