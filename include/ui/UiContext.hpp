#pragma once
#include <cstdio>
#include <cstring>
#include <vector>

#include "platform/Window.hpp"
#include "render/UiRenderer.hpp"
#include "ui/OverlayGuard.hpp"

namespace s3d {

// Single line text field with its own buffer and focus flag.
struct TextField {
    int id = 0;
    char buffer[64] = {};
    int length = 0;
    bool editing = false;

    void clear() {
        buffer[0] = '\0';
        length = 0;
        editing = false;
    }
    void set(const char* text) {
        std::snprintf(buffer, sizeof(buffer), "%s", text ? text : "");
        length = (int)std::strlen(buffer);
    }
    const char* text() const { return buffer; }
    bool empty() const { return length == 0; }
};

// Immediate-mode widget layer shared by every editor panel: owns the input
// snapshot, widget hover/active ids and the inline numeric editor. Panels only
// describe geometry and let this class resolve interaction.
class UiContext {
public:
    // --- frame boundaries ---------------------------------------------------
    // Sampled once per frame, before the world steps; widgets then resolve
    // interaction against this snapshot while they draw.
    void beginInput(Window& window);

    Window* window() const { return window_; }

    // --- input snapshot -----------------------------------------------------
    float pointerX() const { return pointerX_; }
    float pointerY() const { return pointerY_; }
    bool pointerDown() const { return pointerDown_; }
    // The two edges below are what a widget reacts to, and they are false while a
    // popup owns the pointer - see setPointerCapture().
    //
    // They are ALSO false when the pointer is over a blocking overlay. That is the
    // whole point of the guard: the topmost surface under the pointer takes the
    // click, and everything drawn below it goes inert without being asked. A
    // non-blocking overlay (a suggestion list) does not suppress - a click that
    // misses it belongs to the surface underneath.
    bool pointerPressed() const {
        return pointerPressed_ && captureId_ == 0 &&
               !overlays_.pressTaken(pointerX_, pointerY_, overlayOwner_);
    }
    bool pointerReleased() const {
        return pointerReleased_ && captureId_ == 0 &&
               !overlays_.pressTaken(pointerX_, pointerY_, overlayOwner_);
    }
    // The same edges with the capture ignored. Only the popup that asked for the
    // capture reads these; every other reader wants the guarded version.
    bool rawPointerPressed() const { return pointerPressed_; }
    bool rawPointerReleased() const { return pointerReleased_; }
    // The press, asked on behalf of a surface.
    //
    // The same edge as pointerPressed(), but the surface the asker is inside
    // does not take the press from it. A floating window is a surface, and the
    // drag of its title bar is a press of that window: asked as anybody else,
    // the window takes the press away from itself, and no floating window can
    // be moved at all.
    bool pointerPressedBy(int owner) const {
        return pointerPressed_ && captureId_ == 0 &&
               !overlays_.pressTaken(pointerX_, pointerY_, owner);
    }
    double scrollDelta() const { return scrollDelta_; }
    bool ctrlHeld() const { return ctrlHeld_; }
    bool cameraCaptured() const { return cameraCaptured_; }

    // --- who is asking ------------------------------------------------------
    // The pass that is drawing right now says who it is, and the surfaces that
    // belong to it do not take the press from the widgets inside them.
    //
    // A panel that took the press from its own widgets would have a Close
    // button that cannot be pressed, and a floating window that took the press
    // from the panel below it would be a window that cannot be reached.
    void setOverlayOwner(int owner) { overlayOwner_ = owner; }
    int overlayOwner() const { return overlayOwner_; }

    // --- popup capture ------------------------------------------------------
    // A dropdown is drawn last in the frame, so every widget it covers has already
    // run its hit test - which is why a click on a menu row also pressed the
    // button sitting underneath it. A non-zero capture says "something above the
    // widgets owns the pointer": buttons, checkboxes, numeric properties and text
    // fields all go inert until it is cleared.
    //
    // The lifetime is one frame, and it has to be set from the popup state that is
    // already known BEFORE anything is drawn: a panel opened this frame cannot be
    // what suppressed the widgets, and clearing it for the popup's own pass is what
    // lets the popup's rows work.
    void setPointerCapture(int id) { captureId_ = id; }
    int pointerCapture() const { return captureId_; }
    bool captured() const { return captureId_ != 0; }
    // Claim without clearing: zero means "I have nothing to say", which is how a
    // pass that draws its own overlays first and re-arms afterwards stays in charge.
    void claimPointer(int id) { if (id != 0) captureId_ = id; }

    // The rule for "which surface owns this click", in one place. See
    // OverlayGuard.hpp for why it is not done by hand at each call site.
    OverlayGuard& overlays() { return overlays_; }
    const OverlayGuard& overlays() const { return overlays_; }
    // True when the press belongs to a surface drawn above the widgets, so a
    // button under an open dropdown stays inert. The surface the caller is
    // inside does not count: a panel does not take the press from its own
    // widgets.
    bool pressOnOverlay() const { return overlays_.pressTaken(pointerX_, pointerY_, overlayOwner_); }
    bool hoverOnOverlay() const { return overlays_.overAny(pointerX_, pointerY_); }

    // --- scrolling clips ----------------------------------------------------
    // A mirror of UiRenderer::pushClip / popClip, for the hit test rather than the
    // drawing. The renderer can hide geometry it was handed; nothing there can say
    // "is the pointer inside?", so a widget scrolled out of a panel still has a
    // rectangle - visible to a click, invisible to the eye. Any panel that clips its
    // body must clip the hit test with it, or a press drags a property nobody can see.
    void pushClip(const Rect& rect);
    void popClip();
    bool clipped() const { return clipDepth_ > 0; }

    bool hovered(const Rect& rect) const { return clipContains_(rect); }
    void markHovered(int id) { hoveredId_ = id; }
    int hoveredId() const { return hoveredId_; }
    void setActive(int id) { activeId_ = id; }
    int activeId() const { return activeId_; }

    /// Swallow the current click so widgets underneath don't react to it.
    void consumePointerClick() { pointerPressed_ = false; pointerReleased_ = false; activeId_ = 0; }

    // True when a widget has already taken this frame's press.
    //
    // A control that is drawn by hand - the X that closes a panel - asks this
    // before it acts. The widget under the pointer is the one the user is
    // aiming at, and a hand-drawn control that answers the same click as the
    // widget under it is how a button under a button gets pressed: the click
    // closes the panel and presses the button at the same time.
    bool pressClaimedByWidget() const { return activeId_ != 0; }

    // --- widgets ------------------------------------------------------------
    bool button(UiRenderer& ui, int id, float x, float y, float w, float h,
                const char* label, bool toggled = false);
    bool tab(UiRenderer& ui, float x, float y, float w, const char* label, bool active);
    void sectionHeader(UiRenderer& ui, float x, float y, float w, const char* label);
    bool checkbox(UiRenderer& ui, int id, float x, float y, float w, const char* label,
                  bool& value);

    // A pressable area that the caller draws itself.
    //
    // The press is resolved here so that a chip behaves like every other
    // widget: it takes the press, holds it while the pointer is over it, and
    // answers when the pointer is let go. A chip that took the press of
    // something else would steal it from the widget under it.
    //
    // `hovered` says whether the pointer is over the chip, so the caller can
    // draw it as it should look.
    bool chip(UiRenderer& ui, int id, const Rect& rect, bool& hovered);

    // Draggable + typeable numeric property. Returns true when `value` changed.
    bool propertyFloat(UiRenderer& ui, int id, float x, float y, float w, const char* label,
                       float& value, float pixelsPerStep, float lo, float hi);
    // The same, with more decimals than the ordinary float has.
    //
    // The small numbers are lost when they are shown with two: a variation of
    // a thousandth and a variation of a hundredth both say "0.01", and the
    // first of them says "0.00".
    bool propertyFine(UiRenderer& ui, int id, float x, float y, float w, const char* label,
                      float& value, float pixelsPerStep, float lo, float hi);
    // The same for a whole number. `pixelsPerStep` is how much one pixel of
    // drag is worth; the default suits a small number like a count of sides.
    bool propertyInt(UiRenderer& ui, int id, float x, float y, float w, const char* label,
                     int& value, int lo, int hi, float pixelsPerStep = 0.5f);

    // Feeds keyboard/character events into `field` while it is focused.
    void pumpTextField(Window& window, TextField& field, bool fieldHovered);

    // --- the keyboard --------------------------------------------------------
    // One field has the keyboard at a time, and this says whether one has.
    //
    // The window has one keyboard and one stream of characters, and every
    // field in the editor wants them. The field that was clicked has them;
    // the others keep their text and lose the caret. A character that arrives
    // while no field has the keyboard belongs to nobody, and is dropped -
    // otherwise it lands in whatever field is opened next, and the number
    // somebody typed into the search box ends up in the width of the ground.
    bool textFocused() const { return textFocus_ != nullptr; }
    // The field that has the keyboard, or nullptr. The hotkeys ask: a Delete
    // that throws the selection away while somebody is typing a name is not a
    // shortcut, it is a mistake.
    const TextField* focusedField() const { return textFocus_; }

private:
    // Numeric properties share one inline editor; the template keeps the
    // float/int formatting and parsing rules in one place.
    template <typename T>
    bool propertyNumeric(UiRenderer& ui, int id, float x, float y, float w,
                         const char* label, T& value, float pixelsPerStep, T lo, T hi);
    bool closeValueEditorIfDone(bool fieldHovered);

    // What the inline editor has to say about the key that was pressed.
    enum class EditorResult {
        StillEditing,  // the editor keeps the keyboard
        Committed,     // the text in it is the value now
        Cancelled,     // the text in it is worth nothing
    };
    EditorResult valueEditorResult(bool fieldHovered, bool hasFocus);
    // The characters that go into the inline editor, and only into it.
    void pumpValueEditor(Window& window);

    // The field that has the keyboard, and the two things that go with it:
    // taking the keyboard away from whoever had it, and giving it up.
    void takeTextFocus(TextField& field);
    void releaseTextFocus();

    // "%.2f" runs through the full printf float machinery and costs an order of
    // magnitude more than the memcpy it produces, so the formatted display text
    // is memoised against the exact bits it was formatted from. A property sits
    // at rest for almost every frame, so the common answer is a pointer chase.
    // Keyed by widget id, format string and value bits, so a reused id with a
    // different format (ids are shared between sections) can never alias.
    struct ShownText {
        const char* format = nullptr;
        unsigned int bits = 0;
        int id = -1;
        char text[48] = {};
    };
    static constexpr int kShownTextSlots = 16;

    template <typename T>
    const char* cachedShownText(const char* format, T value, int id) {
        static_assert(sizeof(T) == sizeof(unsigned int), "shown text packs value bits");
        unsigned int bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        ShownText& slot = shownText_[(unsigned int)id & (kShownTextSlots - 1)];
        if (slot.id == id && slot.bits == bits && slot.format == format) return slot.text;
        std::snprintf(slot.text, sizeof(slot.text), format, value);
        slot.id = id;
        slot.bits = bits;
        slot.format = format;
        return slot.text;
    }

    float pointerX_ = 0.0f, pointerY_ = 0.0f;
    bool pointerDown_ = false, pointerPressed_ = false, pointerReleased_ = false;
    bool ctrlHeld_ = false, cameraCaptured_ = false;
    double scrollDelta_ = 0.0;
    Window* window_ = nullptr;

    int hoveredId_ = 0;
    int activeId_ = 0;
    // Non-zero while a popup owns the pointer. 0 for the whole of a frame with no
    // popup, which is why every reader can treat it as a plain "is anything open?"
    // flag instead of tracking the popup itself.
    int captureId_ = 0;
    // Clip boxes for the hit test. Four is plenty: a panel clips its own body, and
    // nothing in the editor nests deeper than that.
    static constexpr int kMaxClips = 4;
    Rect clips_[kMaxClips] = {};
    int clipDepth_ = 0;
    // Pushes that arrived after the stack was full. Counted so a pop cannot spend a
    // depth this class never took - same trap as UiRenderer, which is why the two
    // are fixed together.
    int clipOverflow_ = 0;
    // Which surface owns the pointer this frame. Kept here rather than in the
    // editor because the question belongs to the hit test, which is this class.
    OverlayGuard overlays_;
    // Who is drawing right now. The surfaces that belong to this pass do not
    // take the press from the widgets of this pass.
    int overlayOwner_ = 0;
    float dragStartX_ = 0.0f;
    float dragStartValue_ = 0.0f;

    // The pointer, but only where the panel under it is actually allowed to draw.
    bool clipContains_(const Rect& rect) const;

    TextField valueEditor_;
    ShownText shownText_[kShownTextSlots] = {};
    // The field that has the keyboard. A pointer and not an id: the inline
    // editor and the text fields are different objects, and an id would have
    // to be kept apart between them.
    TextField* textFocus_ = nullptr;
    // True when the field that has the keyboard was pumped this frame. A field
    // that was not pumped is a field that is not on the screen any more - the
    // panel was closed - and a keyboard nobody is holding is a keyboard that
    // silences every shortcut in the editor.
    bool focusPumped_ = false;
};

} // namespace s3d