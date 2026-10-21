#include "ui/UiContext.hpp"

#include "core/Str.hpp"
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
    static float raw(float v) { return v; }
    static float fromFloat(float f) { return f; }
    static bool parse(const char* text, float& out) {
        return std::sscanf(text, kScanFormat, &out) == 1;
    }
};

template <>
struct NumTraits<int> {
    static const char* kFormat;
    static const char* kScanFormat;
    static int clampValue(int v, int lo, int hi) { return std::clamp(v, lo, hi); }
    static int step(int from, float delta) { return static_cast<int>(from + delta); }
    static int raw(int v) { return v; }
    static int fromFloat(float f) { return static_cast<int>(f); }
    static bool parse(const char* text, int& out) {
        return std::sscanf(text, kScanFormat, &out) == 1;
    }
};

// A FLOAT WITH MORE DECIMALS THAN THE ORDINARY ONE.
//
// The small numbers are lost when they are shown with two: a variation of a
// thousandth and a variation of a hundredth both say "0.01", and the first
// of them says "0.00". A number that small is a number that has to be shown
// with more of itself.
struct FineFloat {
    float value = 0.0f;

    bool operator==(const FineFloat& other) const { return value == other.value; }
    bool operator!=(const FineFloat& other) const { return value != other.value; }
};

template <>
struct NumTraits<FineFloat> {
    static const char* kFormat;
    static const char* kScanFormat;
    static FineFloat clampValue(FineFloat v, FineFloat lo, FineFloat hi) {
        v.value = clampf(v.value, lo.value, hi.value);
        return v;
    }
    static FineFloat step(FineFloat from, float delta) {
        from.value += delta;
        return from;
    }
    static float raw(FineFloat v) { return v.value; }
    static FineFloat fromFloat(float f) { return FineFloat{f}; }
    static bool parse(const char* text, FineFloat& out) {
        float parsed = 0.0f;
        if (std::sscanf(text, kScanFormat, &parsed) != 1) return false;
        out.value = parsed;
        return true;
    }
};

const char* NumTraits<float>::kFormat = "%.2f";
const char* NumTraits<float>::kScanFormat = "%f";
const char* NumTraits<int>::kFormat = "%d";
const char* NumTraits<int>::kScanFormat = "%d";
const char* NumTraits<FineFloat>::kFormat = "%.4f";
const char* NumTraits<FineFloat>::kScanFormat = "%f";

} // namespace

void UiContext::pushClip(const Rect& rect) {
    if (clipDepth_ >= kMaxClips) {
        ++clipOverflow_;
        return;
    }
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
    if (clipOverflow_ > 0) {
        --clipOverflow_;
        return;
    }
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
    clipOverflow_ = 0;
    // Overlays too: they are declared per frame, by whatever is on screen this
    // frame. Leaving yesterday's rectangles would make a closed dropdown keep
    // swallowing clicks, and the panel behind it would look dead.
    overlays_.clear();
    // captureId_ is deliberately left alone: it describes an open popup, which lives
    // across the frame, and whoever opened it sets it before the first widget runs.

    // The field that had the keyboard and was not pumped last frame is gone: its
    // panel was closed, or it scrolled out of sight. The keyboard goes back to
    // nobody, so that the shortcuts work again.
    if (textFocus_ && !focusPumped_) releaseTextFocus();
    focusPumped_ = false;
}

bool UiContext::button(UiRenderer& ui, int id, float x, float y, float w, float h,
                       const char* label, bool toggled) {
    const Rect rect{x, y, w, h};
    const bool isHovered = hovered(rect);
    if (isHovered) markHovered(id);

    // The press belongs to the widget that took it, and to no other.
    //
    // A widget that did not take the press has no business taking it away
    // from the one that did: the panels are drawn in the order they are
    // stacked, and a panel BELOW a floating one would take the press away
    // from the button of the floating one - which is how a button in a
    // floating window could not be pressed at all.
    if (activeId_ == id) {
        // The widget that has the press gives it up when the pointer leaves
        // it, or when a popup takes the pointer away.
        if (!isHovered || captured()) activeId_ = 0;
    } else if (!captured() && !pressOnOverlay() && pointerPressed_ && isHovered) {
        activeId_ = id;
    }

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

void UiContext::takeTextFocus(TextField& field) {
    // Whoever had the keyboard loses it. The text they typed stays - a field
    // that loses the caret keeps what was written in it - but the caret goes,
    // because the field is not the one being typed into any more.
    //
    // The inline editor is the exception: it keeps its text AND its editing
    // flag, because the number that was typed into it is a number that is
    // worth something. It is committed the next time its field is drawn, so
    // clicking away from a number does not throw the number away.
    if (textFocus_ && textFocus_ != &field && textFocus_ != &valueEditor_)
        textFocus_->editing = false;
    textFocus_ = &field;
    field.editing = true;
    // The characters that arrived before this field had the keyboard belong to
    // nobody: they were typed at a picture of the screen. They are dropped
    // here, and not when the field is closed, because a number that is typed
    // and then a field that is opened has to end up as the number - not as the
    // number with a stray character in front of it.
    if (window_) {
        window_->discardTextInput();
        // The field has the keyboard: the characters that arrive are kept
        // until somebody reads them, so a fast typist does not lose every
        // second letter.
        window_->setTextCaptured(true);
    }
    focusPumped_ = true;
}

void UiContext::releaseTextFocus() {
    // The inline editor keeps its text here too, for the same reason: the
    // number is committed by the field that owns it, and not by the keyboard
    // going back to nobody.
    if (textFocus_ && textFocus_ != &valueEditor_) textFocus_->editing = false;
    textFocus_ = nullptr;
    focusPumped_ = false;
    // Nobody has the keyboard now, so a character that arrives belongs to
    // nobody and is thrown away with the rest of them.
    if (window_) window_->setTextCaptured(false);
}

void UiContext::pumpTextField(Window& window, TextField& field, bool fieldHovered) {
    // The keyboard belongs to one field at a time. A press on the field gives
    // it the keyboard; a press anywhere else takes the keyboard away. The
    // field that had it keeps its text, because the filter it filters with is
    // still the filter the user meant.
    if (pointerPressed()) {
        if (fieldHovered) {
            if (textFocus_ != &field) takeTextFocus(field);
            activeId_ = field.id;
        } else if (textFocus_ == &field) {
            releaseTextFocus();
        }
    }

    // Not the field with the keyboard: nothing that is typed goes in here.
    // This is the rule that stops the text from passing through one field
    // and into another - the characters used to be taken by whichever field
    // was pumped first, and the field being typed into was often not that
    // one.
    if (textFocus_ != &field) {
        field.editing = false;
        return;
    }
    focusPumped_ = true;

    char typed[64];
    const int count = window.consumeTextInput(typed, (int)sizeof(typed) - 1);
    for (int i = 0; i < count; ++i)
        if (field.length < (int)sizeof(field.buffer) - 2)
            field.buffer[field.length++] = typed[i];

    // The backspace takes off the last CHARACTER and not the last byte: a
    // letter that is cut in half is a letter the engine cannot draw, and a
    // field that holds one cannot be typed into again.
    if (window.takeKeyPress(GLFW_KEY_BACKSPACE) && field.length > 0) {
        field.length = str::utf8WithoutLast(field.buffer, field.length);
        field.buffer[field.length] = '\0';
    }
    field.buffer[field.length] = '\0';
}

void UiContext::pumpValueEditor(Window& window) {
    // Only the field with the keyboard gets the characters. The inline editor
    // is a field like any other, and the two of them are never both the one
    // with the keyboard.
    if (textFocus_ != &valueEditor_) return;
    focusPumped_ = true;

    char typed[64];
    const int count = window.consumeTextInput(typed, (int)sizeof(typed) - 1);
    for (int i = 0; i < count; ++i)
        if (valueEditor_.length < (int)sizeof(valueEditor_.buffer) - 2)
            valueEditor_.buffer[valueEditor_.length++] = typed[i];

    if (window.takeKeyPress(GLFW_KEY_BACKSPACE) && valueEditor_.length > 0) {
        valueEditor_.length = str::utf8WithoutLast(valueEditor_.buffer, valueEditor_.length);
        valueEditor_.buffer[valueEditor_.length] = '\0';
    }
    valueEditor_.buffer[valueEditor_.length] = '\0';
}

UiContext::EditorResult UiContext::valueEditorResult(bool fieldHovered, bool hasFocus) {
    Window& window = *window_;

    // The editor does not have the keyboard any more: somebody clicked on
    // another field, or the panel with the editor was closed. The number that
    // was typed is the number now - clicking away from it does not throw it
    // away.
    if (!hasFocus) {
        valueEditor_.editing = false;
        return EditorResult::Committed;
    }

    if (window.takeKeyPress(GLFW_KEY_ESCAPE)) {
        // The text is worth nothing: the editor goes back to the value that
        // was there before.
        valueEditor_.clear();
        return EditorResult::Cancelled;
    }
    if (window.takeKeyPress(GLFW_KEY_ENTER) || (pointerPressed() && !fieldHovered)) {
        // The text is the value now. The buffer is left alone: it is read by
        // the caller before it is cleared, and the value it holds is the one
        // that was typed.
        valueEditor_.editing = false;
        return EditorResult::Committed;
    }
    return EditorResult::StillEditing;
}

bool UiContext::closeValueEditorIfDone(bool fieldHovered) {
    const EditorResult result = valueEditorResult(fieldHovered, textFocus_ == &valueEditor_);
    return result != EditorResult::StillEditing;
}

template <typename T>
bool UiContext::propertyNumeric(UiRenderer& ui, int id, float x, float y, float w,
                                const char* label, T& value, float pixelsPerStep, T lo,
                                T hi) {
    using Traits = NumTraits<T>;

    // The name of the number and the number itself share the row.
    //
    // The name is fitted into the room that is left before the number, and the
    // number's box gives room back when there is not enough: a name that is
    // laid over a number is a name nobody can read, and a panel that is too
    // narrow for both of them is a panel whose numbers cannot be read either.
    const float labelX = x + 8.0f;
    float fieldX = x + w * 0.40f;
    float fieldW = w - w * 0.40f - 6.0f;
    constexpr float kMinLabelRoom = 54.0f;
    if (fieldX - labelX - 6.0f < kMinLabelRoom) {
        fieldX = labelX + kMinLabelRoom + 6.0f;
        fieldW = std::max(30.0f, x + w - 6.0f - fieldX);
    }
    const bool fieldHovered = hovered(Rect{fieldX, y, fieldW, theme::fieldHeight});

    const float labelRoom = std::max(0.0f, fieldX - labelX - 6.0f);
    ui.textFit(labelX, y + 2.0f, labelRoom, label, theme::textDim);
    if (fieldHovered) markHovered(id);

    // --- inline text editor -------------------------------------------------
    if (valueEditor_.editing && valueEditor_.id == id) {
        // Does the editor still have the keyboard? If it does not, somebody
        // has clicked somewhere else and the number that was typed is the
        // number now.
        const bool hasFocus = textFocus_ == &valueEditor_;
        // The characters go into the editor before it is drawn, so the number
        // that is on screen is the number that has been typed - not the one
        // from the frame before.
        if (hasFocus && window_) pumpValueEditor(*window_);

        ui.fillRect(fieldX, y, fieldW, theme::fieldHeight, theme::accent.scaled(0.3f));
        ui.text(fieldX + 6.0f, y + 2.0f, valueEditor_.text(), theme::text);
        ui.fillRect(fieldX + 7.0f + ui.textWidth(valueEditor_.text()), y + 4.0f, 1.5f, 14.0f,
                    theme::accent.scaled(0.8f));

        const EditorResult result = valueEditorResult(fieldHovered, hasFocus);
        if (result == EditorResult::StillEditing) return false;

        // The text is read out of the editor BEFORE the buffer is cleared, and
        // it is read whether the editor was committed or thrown away: the
        // difference between the two is the answer, not the text.
        //
        // The check used to ask whether the editor was still editing, and the
        // answer was always no - committing it had already stopped it. So the
        // number that was typed was never the number that was kept, and
        // everything somebody typed was thrown away the moment they let go
        // of the key.
        T parsed{};
        const bool parsedOk = result == EditorResult::Committed &&
                              valueEditor_.text()[0] != '\0' &&
                              Traits::parse(valueEditor_.text(), parsed);
        valueEditor_.clear();
        releaseTextFocus();
        if (!parsedOk) return false;

        parsed = Traits::clampValue(parsed, lo, hi);
        if (parsed == value) return false;
        value = parsed;
        return true;
    }
    // The editor belongs to another field, and it is left alone. Clearing it
    // here closed it before a single character was typed: a panel draws one
    // field after another, and the field after the one being edited closed it.

    // --- drag interaction ---------------------------------------------------
    // The guarded press: a field under a window that is on top of it is not a
    // field that can be dragged, and the click belongs to the window.
    //
    // And the press belongs to the widget that took it: a panel below a
    // floating one must not take the press away from the field in the
    // floating one, or the field in the floating one could not be dragged at
    // all.
    if (activeId_ == id) {
        if (captured()) activeId_ = 0;
    } else if (!captured() && pointerPressed() && fieldHovered) {
        activeId_ = id;
        dragStartX_ = pointerX_;
        dragStartValue_ = Traits::raw(value);
    }

    bool changed = false;
    if (activeId_ == id && pointerDown_) {
        const T candidate = Traits::clampValue(
            Traits::step(Traits::fromFloat(dragStartValue_),
                         (pointerX_ - dragStartX_) * pixelsPerStep),
            lo, hi);
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
                          Traits::kFormat, Traits::raw(value));
            valueEditor_.length = (int)std::strlen(valueEditor_.buffer);
            valueEditor_.id = id;
            // The editor takes the keyboard. It is the field that is being
            // typed into now, and the field that had it - the search box
            // somebody was typing a name into - gives it up.
            takeTextFocus(valueEditor_);
        }
        activeId_ = 0;
    }

    ui.fillRect(fieldX, y, fieldW, theme::fieldHeight,
                activeId_ == id ? theme::accent.scaled(0.5f) : theme::field);
    ui.text(fieldX + 6.0f, y + 2.0f, cachedShownText(Traits::kFormat, Traits::raw(value), id),
            theme::text);
    return changed;
}

bool UiContext::propertyFloat(UiRenderer& ui, int id, float x, float y, float w,
                              const char* label, float& value, float pixelsPerStep,
                              float lo, float hi) {
    return propertyNumeric(ui, id, x, y, w, label, value, pixelsPerStep, lo, hi);
}

bool UiContext::propertyFine(UiRenderer& ui, int id, float x, float y, float w,
                              const char* label, float& value, float pixelsPerStep,
                              float lo, float hi) {
    // The number is a float and the field is a fine one, so it goes in and
    // comes out through a little box of its own.
    FineFloat fine{value};
    const bool changed = propertyNumeric(ui, id, x, y, w, label, fine, pixelsPerStep,
                                         FineFloat{lo}, FineFloat{hi});
    if (changed) value = fine.value;
    return changed;
}

bool UiContext::propertyInt(UiRenderer& ui, int id, float x, float y, float w,
                            const char* label, int& value, int lo, int hi,
                            float pixelsPerStep) {
    return propertyNumeric(ui, id, x, y, w, label, value, pixelsPerStep, lo, hi);
}

} // namespace s3d