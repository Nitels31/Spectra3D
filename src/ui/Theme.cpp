#include "Theme.hpp"

#include <cstring>

namespace s3d {
namespace theme {
namespace {

// Every palette lists the same colours in the same order, so one struct and one
// assignment switch all of them. A missing entry would be a compile error here
// rather than a colour left over from the previous theme.
struct Palette {
    Color background, panel, panelDark, header, button, buttonHover, buttonPressed;
    Color accent, accentDim, text, textDim, separator, field;
    Color success, warning, error;
};

// The original dark editor palette.
constexpr Palette kDark{
    {0.12f, 0.12f, 0.14f},  // background
    {0.16f, 0.17f, 0.20f},  // panel
    {0.13f, 0.14f, 0.17f},  // panelDark
    {0.19f, 0.21f, 0.25f},  // header
    {0.24f, 0.27f, 0.33f},  // button
    {0.30f, 0.34f, 0.42f},  // buttonHover
    {0.18f, 0.20f, 0.25f},  // buttonPressed
    {0.33f, 0.55f, 0.85f},  // accent
    {0.22f, 0.33f, 0.50f},  // accentDim
    {0.85f, 0.87f, 0.90f},  // text
    {0.50f, 0.53f, 0.58f},  // textDim
    {0.08f, 0.09f, 0.11f},  // separator
    {0.11f, 0.12f, 0.15f},  // field
    {0.30f, 0.70f, 0.40f},  // success
    {0.85f, 0.70f, 0.25f},  // warning
    {0.85f, 0.35f, 0.30f},  // error
};

// Light: for a bright room, where a dark UI is a lamp pointed at the face. The
// accents are darkened so they still read as "selected" against white.
constexpr Palette kLight{
    {0.90f, 0.91f, 0.93f},  // background
    {1.00f, 1.00f, 1.00f},  // panel
    {0.95f, 0.95f, 0.96f},  // panelDark
    {0.91f, 0.92f, 0.94f},  // header
    {0.87f, 0.88f, 0.90f},  // button
    {0.79f, 0.82f, 0.87f},  // buttonHover
    {0.82f, 0.83f, 0.86f},  // buttonPressed
    {0.16f, 0.40f, 0.78f},  // accent
    {0.78f, 0.86f, 0.95f},  // accentDim
    {0.10f, 0.12f, 0.15f},  // text
    {0.42f, 0.45f, 0.50f},  // textDim
    {0.80f, 0.82f, 0.86f},  // separator
    {1.00f, 1.00f, 1.00f},  // field
    {0.10f, 0.55f, 0.20f},  // success
    {0.75f, 0.55f, 0.05f},  // warning
    {0.75f, 0.15f, 0.10f},  // error
};

// Contrast: near-black surfaces with pure white text, for a laptop on battery in
// daylight where a dark grey UI washes out completely.
constexpr Palette kContrast{
    {0.00f, 0.00f, 0.00f},  // background
    {0.06f, 0.06f, 0.08f},  // panel
    {0.10f, 0.10f, 0.12f},  // panelDark
    {0.18f, 0.18f, 0.20f},  // header
    {0.28f, 0.28f, 0.30f},  // button
    {0.40f, 0.40f, 0.42f},  // buttonHover
    {0.10f, 0.10f, 0.12f},  // buttonPressed
    {1.00f, 0.85f, 0.20f},  // accent
    {0.60f, 0.50f, 0.00f},  // accentDim
    {1.00f, 1.00f, 1.00f},  // text
    {0.78f, 0.78f, 0.78f},  // textDim
    {0.35f, 0.35f, 0.35f},  // separator
    {0.00f, 0.00f, 0.00f},  // field
    {0.20f, 1.00f, 0.40f},  // success
    {1.00f, 0.85f, 0.20f},  // warning
    {1.00f, 0.30f, 0.25f},  // error
};

ThemeId g_active = ThemeId::Dark;

const Palette& paletteFor(ThemeId id) {
    switch (id) {
        case ThemeId::Light: return kLight;
        case ThemeId::Contrast: return kContrast;
        case ThemeId::Dark:
        default: return kDark;
    }
}

void apply(const Palette& p) {
    background = p.background;
    panel = p.panel;
    panelDark = p.panelDark;
    header = p.header;
    button = p.button;
    buttonHover = p.buttonHover;
    buttonPressed = p.buttonPressed;
    accent = p.accent;
    accentDim = p.accentDim;
    text = p.text;
    textDim = p.textDim;
    separator = p.separator;
    field = p.field;
    success = p.success;
    warning = p.warning;
    error = p.error;
}

} // namespace

Color background;
Color panel;
Color panelDark;
Color header;
Color button;
Color buttonHover;
Color buttonPressed;
Color accent;
Color accentDim;
Color text;
Color textDim;
Color separator;
Color field;
Color success;
Color warning;
Color error;

ThemeId activeTheme() { return g_active; }

void setActiveTheme(ThemeId id) {
    g_active = id;
    apply(paletteFor(id));
}

const char* themeName(ThemeId id) {
    switch (id) {
        case ThemeId::Light: return "light";
        case ThemeId::Contrast: return "contrast";
        case ThemeId::Dark:
        default: return "dark";
    }
}

ThemeId themeFromName(const char* name) {
    if (!name) return ThemeId::Dark;
    if (std::strcmp(name, "light") == 0) return ThemeId::Light;
    if (std::strcmp(name, "contrast") == 0) return ThemeId::Contrast;
    return ThemeId::Dark;
}

} // namespace theme
} // namespace s3d