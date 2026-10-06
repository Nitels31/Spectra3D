#pragma once
#include "core/Math.hpp"

namespace s3d {

// Surface response of a body. `isStatic` turns the body into an immovable
// collider that still pushes dynamic bodies.
struct Material {
    float density = 1.0f;
    float friction = 0.5f;
    float restitution = 0.3f;
    bool isStatic = false;

    Vec3 color{0.7f, 0.7f, 0.7f};
};

} // namespace s3d