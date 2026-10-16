#include "ui/UiContext.hpp"

#include "ui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace s3d {
namespace {

// Per-type formatting/parsing/stepping for the numeric property widget.
template <typename T>
struct NumTraits;

template <>
struct NumTraits<float> {
    static const char* kFormat;
    static const char* kScanFormat;
    static float clampValue(float v, float lo, float hi) { return clampf(v, lo, hi); }
    static float step(float from, float delta) { return from + delta; }
};

template <>
struct NumTraits<int> {
    static const char* kFormat;
    static const char* kScanFormat;
    static int clampValue(int v, int lo, int hi) { return std::clamp(v, lo, hi); }
    static int step(int from, float delta) { return static_cast<int>(from + delta); }
};

const char* NumTraits<float>::kFormat = "%.2f";
const char* NumTraits<float>::kScanFormat = "%f";
const char* NumTraits<int>::kFormat = "%d";
const char* NumTraits<int>::kScanFormat = "%d";

} // namespace

void UiContext::pushClip(const Rect& rect) {
    if (clipDepth_ >= kMaxClips) return;
    Rect next = rect;
    if (clipDepth_ > 0) {
        const Rect& outer = clips_[clipDepth_ - 1];
        const float left = std::fmax(outer.x, next.x);
        const float top = std::fmax(outer.y, next.y);
        const float right = std::fmin(outer.x + outer.width, next.x + next.width);
        const float bottom = std::fmin(outer.y + outer.height, next.y + next.height);
        next = Rect{left, top, std::fmax(0.0f, right - left), std::fmax(0.0f, bottom - top)};
    }
    clips_[clipDepth_++] = next;
}

void UiContext::popClip() {
    if (clipDepth_ > 0) --clipDepth_;
}

bool UiContext::clipContains_(const Rect& rect) const {
    if (clipDepth_ == 0) return rect.contains(pointerX_, pointerY_);
    // The clipped test, not the two tests joined: a rectangle that only overlaps the
    // clip box is outside it as far as a click is concerned, and `contains &&
    // clip.contains` would accept the sliver where the two meet.
    const Rect& clip = clips_[clipDepth_ - 1];
    if (pointerX_ < clip.x || pointerX_ >= clip.x + clip.width) return false;
    if (pointerY_ < clip.y || pointerY_ >= clip.y + clip.height) return false;
    return rect.contains(pointerX_, pointerY_);
}

void UiContext::beginInput(Window& window) {
    window_ = &window;
    double x = 0.0, y = 0.0;
    window.cursorPos(x, y);
    pointerX_ = (float)x;
    pointerY_ = (float)y;
    pointerDown_ = window.mouseDown(GLFW_MOUSE_BUTTON_LEFT);
    pointerPressed_ = window.mousePressed(GLFW_MOUSE_BUTTON_LEFT);
    pointerReleased_ = window.mouseReleased(GLFW_MOUSE_BUTTON_LEFT);
    scrollDelta_ = window.consumeScroll();
    cameraCaptured_ = window.mouseCaptured();
    ctrlHeld_ = window.keyDown(GLFW_KEY_LEFT_CONTROL) || window.keyDown(GLFW_KEY_RIGHT_CONTROL);
    hoveredId_ = 0;
    // The clip stack is not: a panel that clips its body pushes and pops around its
    // own sections, so it is always empty again by the time the next frame starts -
    // and a leftover box would silently make half the window unclickable.
    clipDepth_ = 0;
    // captureId_ is deliberately left alone: it describes an open popup, which lives
    // across the frame, and whoever opened it sets it before the first widget runs.
}

bool UiContext::button(UiRenderer& ui, int id, float x, float y, float w, float h,
                       const char* label, bool toggled) {
    const Rect rect{x, y, w, h};
    const bool isHovered = hovered(rect);
    if (isHovered) markHovered(id);
    // Under a popup a button still looks like a button - a disabled one would
    // flicker on and off as the list opens - it just does not answer.
    if (captured()) activeId_ = 0;
    else if (pointerPressed_ && isHovered) activeId_ = id;

    bool clicked = false;
    if (activeId_ == id && pointerReleased_) {
        clicked = isHovered;
        activeId_ = 0;
    }

    Color background = theme::button;
    if (toggled) background = theme::accentDim;
    else if (activeId_ == id) background = theme::buttonPressed;
    else if (isHovered) background = theme::buttonHover;

    ui.fillRect(x, y, w, h, background);

    // Measure, never assume. A label that does not fit is shrunk rather than
    // allowed to spill over its neighbours: "Без обмежень" is 60 pixels wider
    // than "Off", and a fixed guess that suits one ruins the other.
    const float textWidth = ui.textWidth(label);
    const float slot = std::fmax(0.0f, w - 8.0f);
    const float scale = textWidth > slot ? std::fmax(UiRenderer::kMinTextScale, slot / textWidth) : 1.0f;
    const float drawnWidth = textWidth * scale;
    const float textX = x + (w - drawnWidth) * 0.5f;
    ui.textScaled(textX, y + (h - theme::textLineHeight * scale) * 0.5f, label, theme::text, scale);
    return clicked;
}

bool UiContext::tab(UiRenderer& ui, float x, float y, float w, const char* label,
                    bool active) {
    constexpr float kHeight = 24.0f;
    ui.fillRect(x, y, w, kHeight, active ? theme::panel : theme::panelDark);
    if (active) ui.fillRect(x, y + kHeight - 2.0f, w, 2.0f, theme::accent);
    ui.text(x + 8.0f, y + 4.0f, label, active ? theme::text : theme::textDim);
    return pointerPressed() && hovered(Rect{x, y, w, kHeight});
}

void UiContext::sectionHeader(UiRenderer& ui, float x, float y, float w,
                              const char* label) {
    ui.fillRect(x, y, w, 22.0f, theme::header);
    ui.fillRect(x, y, 3.0f, 22.0f, theme::accent);
    ui.text(x + 12.0f, y + 4.0f, label, theme::text);
}

bool UiContext::checkbox(UiRenderer& ui, int id, float x, float y, float w,
                         const char* label, bool& value) {
    constexpr float kBox = 16.0f;
    const float boxX = x + 8.0f;
    const float boxY = y + 2.0f;
    const bool isHovered = hovered(Rect{boxX, boxY, kBox, kBox});
    if (isHovered) markHovered(id);

    bool changed = false;
    if (pointerPressed() && isHovered) {
        value = !value;
        changed = true;
    }

    ui.fillRect(boxX, boxY, kBox, kBox, theme::field);
    if (value) ui.fillRect(boxX + 2.0f, boxY + 2.0f, kBox - 4.0f, kBox - 4.0f, theme::accent);
    ui.text(boxX + kBox + 6.0f, y + 2.0f, label, theme::textDim);
    return changed;
}

void UiContext::pumpTextField(Window& window, TextField& field, bool fieldHovered) {
    // A popup press is not a press on the field, so a field under an open list keeps
    // its focus instead of blinking out of it the moment the list opens.
    if (pointerPressed() && fieldHovered) {
        field.editing = true;
        activeId_ = field.id;
    }
    if (pointerPressed() && !fieldHovered) field.editing = false;
    if (!field.editing) return;

    char typed[64];
    const int count = window.consumeTextInput(typed, (int)sizeof(typed) - 1);
    for (int i = 0; i < count; ++i)
        if (field.length < (int)sizeof(field.buffer) - 2)
            field.buffer[field.length++] = typed[i];

    if (window.takeKeyPress(GLFW_KEY_BACKSPACE) && field.length > 0)
        field.buffer[--field.length] = '\0';
    field.buffer[field.length] = '\0';
}

bool UiContext::closeValueEditorIfDone(bool fieldHovered) {
    TextField& editor = valueEditor_;
    Window& window = *window_;

    char typed[64];
    const int count = window.consumeTextInput(typed, (int)sizeof(typed) - 1);
    for (int i = 0; i < count; ++i)
        if (editor.length < (int)sizeof(editor.buffer) - 2)
            editor.buffer[editor.length++] = typed[i];

    if (window.takeKeyPress(GLFW_KEY_BACKSPACE) && editor.length > 0)
        editor.buffer[--editor.length] = '\0';
    editor.buffer[editor.length] = '\0';

    if (window.takeKeyPress(GLFW_KEY_ESCAPE)) {
        editor.clear();
        return true;
    }
    if (window.takeKeyPress(GLFW_KEY_ENTER) || (pointerPressed() && !fieldHovered)) {
        editor.editing = false;
        return true;
    }
    return false;
}

template <typename T>
bool UiContext::propertyNumeric(UiRenderer& ui, int id, float x, float y, float w,
                                const char* label, T& value, float pixelsPerStep, T lo,
                                T hi) {
    using Traits = NumTraits<T>;

    const float fieldX = x + w * 0.40f;
    const float fieldW = w - w * 0.40f - 6.0f;
    const bool fieldHovered = hovered(Rect{fieldX, y, fieldW, theme::fieldHeight});

    ui.text(x + 8.0f, y + 2.0f, label, theme::textDim);
    if (fieldHovered) markHovered(id);

    // --- inline text editor -------------------------------------------------
    if (valueEditor_.editing && valueEditor_.id == id) {
        ui.fillRect(fieldX, y, fieldW, theme::fieldHeight, theme::accent.scaled(0.3f));
        ui.text(fieldX + 6.0f, y + 2.0f, valueEditor_.text(), theme::text);
        ui.fillRect(fieldX + 7.0f + ui.textWidth(valueEditor_.text()), y + 4.0f, 1.5f, 14.0f,
                    theme::accent.scaled(0.8f));

        const bool closed = closeValueEditorIfDone(fieldHovered);
        if (!closed) return false;

        // Parsed straight out of the editor buffer, before the clear() below
        // overwrites it: the committed text used to be copied into a std::string
        // for exactly this reason.
        const bool hadText = valueEditor_.text()[0] != '\0' && valueEditor_.editing;
        T parsed{};
        const bool parsedOk =
            hadText && std::sscanf(valueEditor_.text(), Traits::kScanFormat, &parsed) == 1;
        valueEditor_.clear();
        if (!parsedOk) return false;

        parsed = Traits::clampValue(parsed, lo, hi);
        if (parsed == value) return false;
        value = parsed;
        return true;
    }
    valueEditor_.clear();

    // --- drag interaction ---------------------------------------------------
    if (captured()) activeId_ = 0;
    else if (pointerPressed_ && fieldHovered) {
        activeId_ = id;
        dragStartX_ = pointerX_;
        dragStartValue_ = (float)value;
    }

    bool changed = false;
    if (activeId_ == id && pointerDown_) {
        const T candidate = Traits::clampValue(
            Traits::step((T)dragStartValue_, (pointerX_ - dragStartX_) * pixelsPerStep), lo, hi);
        if (candidate != value) {
            value = candidate;
            changed = true;
        }
    }
    if (pointerReleased() && activeId_ == id) {
        if (std::fabs(pointerX_ - dragStartX_) < 3.0f) {
            // Formatted straight into the editor buffer: the intermediate copy
            // plus TextField::set() was a second snprintf of the same string.
            std::snprintf(valueEditor_.buffer, sizeof(valueEditor_.buffer),
                          Traits::kFormat, value);
            valueEditor_.length = (int)std::strlen(valueEditor_.buffer);
            valueEditor_.id = id;
            valueEditor_.editing = true;
        }
        activeId_ = 0;
    }

    ui.fillRect(fieldX, y, fieldW, theme::fieldHeight,
                activeId_ == id ? theme::accent.scaled(0.5f) : theme::field);
    ui.text(fieldX + 6.0f, y + 2.0f, cachedShownText(Traits::kFormat, value, id), theme::text);
    return changed;
}

bool UiContext::propertyFloat(UiRenderer& ui, int id, float x, float y, float w,
                              const char* label, float& value, float pixelsPerStep,
                              float lo, float hi) {
    return propertyNumeric(ui, id, x, y, w, label, value, pixelsPerStep, lo, hi);
}

bool UiContext::propertyInt(UiRenderer& ui, int id, float x, float y, float w,
                            const char* label, int& value, int lo, int hi) {
    return propertyNumeric(ui, id, x, y, w, label, value, 0.5f, lo, hi);
}

} // namespace s3d