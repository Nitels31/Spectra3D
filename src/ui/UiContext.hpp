#pragma once
#include <cstdio>
#include <cstring>

#include "../platform/Window.hpp"
#include "../render/UiRenderer.hpp"

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
    bool pointerPressed() const { return pointerPressed_; }
    bool pointerReleased() const { return pointerReleased_; }
    double scrollDelta() const { return scrollDelta_; }
    bool ctrlHeld() const { return ctrlHeld_; }
    bool cameraCaptured() const { return cameraCaptured_; }

    bool hovered(const Rect& rect) const { return rect.contains(pointerX_, pointerY_); }
    void markHovered(int id) { hoveredId_ = id; }
    int hoveredId() const { return hoveredId_; }
    void setActive(int id) { activeId_ = id; }
    int activeId() const { return activeId_; }

    // --- widgets ------------------------------------------------------------
    bool button(UiRenderer& ui, int id, float x, float y, float w, float h,
                const char* label, bool toggled = false);
    bool tab(UiRenderer& ui, float x, float y, float w, const char* label, bool active);
    void sectionHeader(UiRenderer& ui, float x, float y, float w, const char* label);
    bool checkbox(UiRenderer& ui, int id, float x, float y, float w, const char* label,
                  bool& value);

    // Draggable + typeable numeric property. Returns true when `value` changed.
    bool propertyFloat(UiRenderer& ui, int id, float x, float y, float w, const char* label,
                       float& value, float pixelsPerStep, float lo, float hi);
    bool propertyInt(UiRenderer& ui, int id, float x, float y, float w, const char* label,
                     int& value, int lo, int hi);

    // Feeds keyboard/character events into `field` while it is focused.
    void pumpTextField(Window& window, TextField& field, bool fieldHovered);


private:
    // Numeric properties share one inline editor; the template keeps the
    // float/int formatting and parsing rules in one place.
    template <typename T>
    bool propertyNumeric(UiRenderer& ui, int id, float x, float y, float w,
                         const char* label, T& value, float pixelsPerStep, T lo, T hi);
    bool closeValueEditorIfDone(bool fieldHovered);

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
    float dragStartX_ = 0.0f;
    float dragStartValue_ = 0.0f;

TextField valueEditor_;
    ShownText shownText_[kShownTextSlots] = {};
};

} // namespace s3d