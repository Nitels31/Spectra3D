#pragma once
namespace s3d {

// RGB with alpha. Lives in its own header because both the 3D scene renderer and
// the batched UI renderer take colours as parameters.
struct Color {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float ir, float ig, float ib, float ia = 1.0f)
        : r(ir), g(ig), b(ib), a(ia) {}

    constexpr Color scaled(float factor) const {
        return {r * factor, g * factor, b * factor, a};
    }
};

} // namespace s3d