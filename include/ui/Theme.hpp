#pragma once
#include <cstddef>

#include "render/SceneRenderer.hpp"
#include "render/TextAtlas.hpp"

namespace s3d {
namespace theme {

// Palettes. A palette is a plain set of colours, so adding one costs a line here
// and nothing anywhere else.
enum class ThemeId { Dark, Light, Contrast };

// The theme chosen by the user, or Dark when nothing has been chosen yet.
ThemeId activeTheme();
void setActiveTheme(ThemeId id);
const char* themeName(ThemeId id);
ThemeId themeFromName(const char* name);
constexpr size_t themeCount() { return 3; }

// These are variables, not constants, on purpose: switching a theme has to be
// visible to every panel that says `theme::panel`, and there are hundreds of
// those references. A function returning a reference would mean the same edit in
// every one of them; a mutable definition means none.
//
// `ui` is the one place allowed to write them (setActiveTheme); everything else
// reads. That keeps the palette as close to a constant as a switchable value can
// be without a rename of the entire UI layer.
extern Color background;
extern Color panel;
extern Color panelDark;
extern Color header;
extern Color button;
extern Color buttonHover;
extern Color buttonPressed;
extern Color accent;
extern Color accentDim;
extern Color text;
extern Color textDim;
extern Color separator;
extern Color field;
extern Color success;
extern Color warning;
extern Color error;

// Shared metrics, so panels stay aligned without magic numbers everywhere. These
// are deliberately not part of the theme: a light palette does not want a
// different row height.
inline constexpr float rowHeight = 22.0f;
inline constexpr float fieldHeight = 20.0f;
inline constexpr float menuBarHeight = 26.0f;
inline constexpr float leftDockWidth = 260.0f;
inline constexpr float rightDockWidth = 280.0f;
// The height of one line of text. It is the font's number and not the theme's:
// the atlas places the baseline inside a line of exactly this height, and the
// two cannot be told apart without a number that is written down twice.
inline constexpr float textLineHeight = (float)TextAtlas::kLineHeight;

} // namespace theme
} // namespace s3d