#pragma once
#include "../render/SceneRenderer.hpp"

namespace s3d {
namespace theme {

// Dark editor palette. Colours live here (not inline in panel code) so the whole
// UI can be re-skinned from one place.
inline constexpr Color background{0.12f, 0.12f, 0.14f};
inline constexpr Color panel{0.16f, 0.17f, 0.20f};
inline constexpr Color panelDark{0.13f, 0.14f, 0.17f};
inline constexpr Color header{0.19f, 0.21f, 0.25f};
inline constexpr Color button{0.24f, 0.27f, 0.33f};
inline constexpr Color buttonHover{0.30f, 0.34f, 0.42f};
inline constexpr Color buttonPressed{0.18f, 0.20f, 0.25f};
inline constexpr Color accent{0.33f, 0.55f, 0.85f};
inline constexpr Color accentDim{0.22f, 0.33f, 0.50f};
inline constexpr Color text{0.85f, 0.87f, 0.90f};
inline constexpr Color textDim{0.50f, 0.53f, 0.58f};
inline constexpr Color separator{0.08f, 0.09f, 0.11f};
inline constexpr Color field{0.11f, 0.12f, 0.15f};
inline constexpr Color success{0.30f, 0.70f, 0.40f};
inline constexpr Color warning{0.85f, 0.70f, 0.25f};
inline constexpr Color error{0.85f, 0.35f, 0.30f};

// Shared metrics, so panels stay aligned without magic numbers everywhere.
inline constexpr float rowHeight = 22.0f;
inline constexpr float fieldHeight = 20.0f;
inline constexpr float menuBarHeight = 26.0f;
inline constexpr float statusBarHeight = 28.0f;
inline constexpr float leftDockWidth = 260.0f;
inline constexpr float rightDockWidth = 280.0f;
inline constexpr float textLineHeight = 15.0f;

} // namespace theme
} // namespace s3d