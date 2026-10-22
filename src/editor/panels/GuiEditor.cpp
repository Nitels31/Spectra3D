#include "editor/Editor.hpp"

#include "core/Str.hpp"
#include "ui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace s3d {
namespace {

constexpr float kHeaderHeight = 24.0f;
constexpr float kRowStep = theme::rowHeight;

// The handles around an element.
//
// Nine of them: the body of the element, which moves it, and eight around it
// - four corners and four sides - which make it bigger or smaller. The
// numbers are the corners first, clockwise, and then the sides, because the
// corner at the top left is the one everybody reaches for and it should be
// the first thing the code talks about.
enum class Handle { None = -1, Move = 0, TopLeft, Top, TopRight, Right, BottomRight, Bottom,
                    BottomLeft, Left };

// How big a handle is on the screen, in pixels. Big enough to take with a
// mouse, small enough not to cover the element it belongs to.
constexpr float kHandleSize = 8.0f;
// How close to the edge of an element the pointer has to be to take the
// handle, in pixels.
constexpr float kHandleGrab = 5.0f;

// The corner of the element a handle stands at, in the element's own units:
// the fractions of the width and the height, from 0 (left, top) to 1.
void handleAnchor(Handle handle, float& fx, float& fy) {
    switch (handle) {
    case Handle::TopLeft: fx = 0.0f; fy = 0.0f; break;
    case Handle::Top: fx = 0.5f; fy = 0.0f; break;
    case Handle::TopRight: fx = 1.0f; fy = 0.0f; break;
    case Handle::Right: fx = 1.0f; fy = 0.5f; break;
    case Handle::BottomRight: fx = 1.0f; fy = 1.0f; break;
    case Handle::Bottom: fx = 0.5f; fy = 1.0f; break;
    case Handle::BottomLeft: fx = 0.0f; fy = 1.0f; break;
    case Handle::Left: fx = 0.0f; fy = 0.5f; break;
    default: fx = 0.5f; fy = 0.5f; break;
    }
}

// The name of a kind, as the tree shows it.
const char* kindLabel(gui::ElementKind kind) {
    switch (kind) {
        case gui::ElementKind::Panel: return "Panel";
        case gui::ElementKind::Image: return "Image";
        case gui::ElementKind::Text: return "Text";
        case gui::ElementKind::Button: return "Button";
    }
    return "Panel";
}

// How deep an element sits in the tree: how many parents it has.
int elementDepth(const gui::Canvas& canvas, int index) {
    int depth = 0;
    const gui::Element* element = canvas.at(index);
    if (!element) return 0;
    int parent = element->parent;
    while (parent >= 0 && parent < (int)canvas.elements.size() && depth < 32) {
        ++depth;
        const gui::Element* above = canvas.at(parent);
        if (!above) break;
        parent = above->parent;
    }
    return depth;
}

// Are two elements the same, in every way the inspector can change?
bool sameElement(const gui::Element& a, const gui::Element& b) {
    return a.kind == b.kind && a.name == b.name && a.parent == b.parent && a.x == b.x &&
           a.y == b.y && a.width == b.width && a.height == b.height && a.text == b.text &&
           a.texture == b.texture && a.fontSize == b.fontSize && a.visible == b.visible &&
           a.interactable == b.interactable && a.color.r == b.color.r &&
           a.color.g == b.color.g && a.color.b == b.color.b && a.color.a == b.color.a;
}

} // namespace

// The canvas as the screen sees it: the scale and the offset that turn the
// canvas's own units into the pixels of the window.
void Editor::guiFit(const Rect& area, float& scale, float& offsetX, float& offsetY) const {
    if (!canvas_) {
        scale = 1.0f;
        offsetX = area.x;
        offsetY = area.y;
        return;
    }
    canvas_->fit(area, scale, offsetX, offsetY);
}

// The handle under the pointer, or -1.
//
// The handles are found first and the body of the element second: a handle
// stands ON the edge of the element, and an element that is picked by its
// edge would swallow the handle every time.
int Editor::guiHandleAt(const Rect& rect, float px, float py) const {
    const float half = kHandleSize * 0.5f;
    for (int i = 1; i <= 8; ++i) {
        const Handle handle = (Handle)i;
        float fx = 0.5f, fy = 0.5f;
        handleAnchor(handle, fx, fy);
        const float hx = rect.x + rect.width * fx;
        const float hy = rect.y + rect.height * fy;
        if (std::fabs(px - hx) <= half + kHandleGrab && std::fabs(py - hy) <= half + kHandleGrab)
            return i;
    }
    return -1;
}

void Editor::setGuiMode(bool on) {
    if (guiMode_ == on) return;
    guiMode_ = on;
    // The interface and the world are not edited at the same time: leaving the
    // world takes the body's selection with it, and the two selections are
    // two different questions.
    if (on) {
        selectedBodies_.clear();
        activeBody_ = -1;
    } else {
        selectedGui_ = -1;
        guiDragElement_ = -1;
    }
}

// The interface is laid out in the viewport.
//
// A press takes the element under the pointer, or one of the handles around
// the one that is selected. A drag moves it or makes it bigger. A press on
// nothing takes the selection away.
void Editor::handleGuiInput(const Rect& area) {
    if (!canvas_ || !guiMode_) return;
    // The pan tool is the camera's, not the interface's: a drag that moves the
    // picture must not also move the element under it.
    if (tool_ == Tool::Pan) return;
    if (!ui_.hovered(area)) {
        if (ui_.pointerReleased()) guiDragElement_ = -1;
        return;
    }
    // A press on a window that is over the scene belongs to the window.
    if (ui_.pressOnOverlay()) {
        if (ui_.pointerReleased()) guiDragElement_ = -1;
        return;
    }

    const float px = ui_.pointerX();
    const float py = ui_.pointerY();

    if (pressAccepted() && guiDragElement_ < 0) {
        // The handles of the selected element come first: they stand on the
        // edge of it, and an element picked by its edge would take the press
        // away from the handle every time.
        int handle = -1;
        if (selectedGui_ >= 0) {
            const Rect rect = canvas_->screenRect(selectedGui_, area);
            handle = guiHandleAt(rect, px, py);
        }

        if (handle >= 0) {
            guiDragElement_ = selectedGui_;
            guiDragHandle_ = handle;
        } else {
            const int hit = canvas_->pickAt(area, px, py);
            selectedGui_ = hit;
            if (hit < 0) {
                guiDragElement_ = -1;
                return;
            }
            guiDragElement_ = hit;
            guiDragHandle_ = (int)Handle::Move;
        }

        gui::Element* element = canvas_->at(guiDragElement_);
        if (!element) {
            guiDragElement_ = -1;
            return;
        }
        // The element as it was before the drag: the whole drag is one change
        // and Ctrl+Z takes it back in one go.
        guiBefore_ = *element;
        guiBeforeIndex_ = guiDragElement_;
        guiBeforeValid_ = true;
        guiDragStartX_ = element->x;
        guiDragStartY_ = element->y;
        guiDragStartW_ = element->width;
        guiDragStartH_ = element->height;
        guiDragStartPointerX_ = px;
        guiDragStartPointerY_ = py;
        return;
    }

    if (ui_.pointerDown() && guiDragElement_ >= 0) {
        gui::Element* element = canvas_->at(guiDragElement_);
        if (!element) {
            guiDragElement_ = -1;
            return;
        }
        float scale = 1.0f, offsetX = 0.0f, offsetY = 0.0f;
        guiFit(area, scale, offsetX, offsetY);
        if (scale <= 1e-4f) return;

        // The pointer's move, in the canvas's own units.
        const float dx = (px - guiDragStartPointerX_) / scale;
        const float dy = (py - guiDragStartPointerY_) / scale;

        const Handle handle = (Handle)guiDragHandle_;
        if (handle == Handle::Move) {
            element->x = guiDragStartX_ + dx;
            element->y = guiDragStartY_ + dy;
        } else {
            float x = guiDragStartX_;
            float y = guiDragStartY_;
            float w = guiDragStartW_;
            float h = guiDragStartH_;
            const float right = x + w;
            const float bottom = y + h;

            switch (handle) {
            case Handle::TopLeft: x += dx; y += dy; break;
            case Handle::Top: y += dy; break;
            case Handle::TopRight: w += dx; y += dy; break;
            case Handle::Right: w += dx; break;
            case Handle::BottomRight: w += dx; h += dy; break;
            case Handle::Bottom: h += dy; break;
            case Handle::BottomLeft: x += dx; w -= dx; break;
            case Handle::Left: x += dx; w -= dx; break;
            default: break;
            }
            // An element that has been dragged inside out is an element with
            // no width, and the engine would draw nothing at all.
            if (w < 1.0f) {
                w = 1.0f;
                x = std::fmin(x, right - 1.0f);
            }
            if (h < 1.0f) {
                h = 1.0f;
                y = std::fmin(y, bottom - 1.0f);
            }
            element->x = x;
            element->y = y;
            element->width = w;
            element->height = h;
        }
        markSceneDirty();
        return;
    }

    if (ui_.pointerReleased()) {
        // The drag is over: the element as it was before it is one entry on
        // the undo stack.
        if (guiBeforeValid_ && guiBeforeIndex_ == guiDragElement_) {
            const gui::Element* element = canvas_->at(guiDragElement_);
            if (element && !sameElement(*element, guiBefore_))
                rememberGui(guiDragElement_, guiBefore_);
        }
        guiDragElement_ = -1;
        guiDragHandle_ = -1;
        guiBeforeValid_ = false;
    }
}

// The handles around the selected element, and the outline of it.
//
// Drawn by the editor's own renderer, over the canvas the game drew: the
// handles are the editor's, and the canvas is the game's.
void Editor::drawGuiSelection(UiRenderer& ui, const Rect& area) {
    if (!canvas_ || !guiMode_ || selectedGui_ < 0) return;
    const gui::Element* element = canvas_->at(selectedGui_);
    if (!element || !element->visible) return;

    const Rect rect = canvas_->screenRect(selectedGui_, area);
    if (rect.width <= 0.0f || rect.height <= 0.0f) return;

    // The outline: four lines, not a rectangle - a filled rectangle would
    // cover the element it is around.
    const Color line = theme::accent;
    ui.fillRect(rect.x, rect.y, rect.width, 1.0f, line);
    ui.fillRect(rect.x, rect.y + rect.height - 1.0f, rect.width, 1.0f, line);
    ui.fillRect(rect.x, rect.y, 1.0f, rect.height, line);
    ui.fillRect(rect.x + rect.width - 1.0f, rect.y, 1.0f, rect.height, line);

    // The handles.
    const float half = kHandleSize * 0.5f;
    for (int i = 1; i <= 8; ++i) {
        const Handle handle = (Handle)i;
        float fx = 0.5f, fy = 0.5f;
        handleAnchor(handle, fx, fy);
        const float hx = rect.x + rect.width * fx;
        const float hy = rect.y + rect.height * fy;
        const bool active = guiDragElement_ == selectedGui_ && guiDragHandle_ == i;
        ui.fillRect(hx - half, hy - half, kHandleSize, kHandleSize,
                    active ? theme::text : theme::accent);
        ui.fillRect(hx - half, hy - half, kHandleSize, 1.0f, theme::panelDark);
        ui.fillRect(hx - half, hy + half - 1.0f, kHandleSize, 1.0f, theme::panelDark);
    }
}

// The tree of elements, in the hierarchy's place.
//
// The interface of the game is a tree like the world is, and it is shown in
// the same panel: the panel on the left is "what is in this project", and
// what is in it depends on what is being laid out.
void Editor::drawGuiHierarchyDock(UiRenderer& ui, const Rect& dock) {
    ui.fillRect(dock.x, dock.y, dock.width, kHeaderHeight, theme::panelDark);
    ui.text(dock.x + 18.0f, dock.y + 5.0f, text("tab.interface"), theme::text);
    ui.fillRect(dock.x, dock.y + kHeaderHeight - 1.0f, dock.width, 1.0f, theme::separator);

    float y = dock.y + kHeaderHeight + 4.0f;

    // The root: the canvas itself.
    {
        const Rect hit{dock.x + 4.0f, y, dock.width - 8.0f, kRowStep};
        if (ui_.hovered(hit)) ui.fillRect(hit.x, hit.y, hit.width, hit.height, theme::header);
        ui.fillRect(dock.x + 10.0f, y + 5.0f, 10.0f, 10.0f, theme::accent);
        ui.text(dock.x + 28.0f, y + 2.0f, text("gui.canvas"), theme::text);
        if (ui_.hovered(hit) && ui_.pointerPressed()) {
            selectedGui_ = -1;
            selectedBodies_.clear();
            activeBody_ = -1;
        }
        y += kRowStep;
    }

    if (!canvas_ || canvas_->elements.empty()) {
        ui.textFit(dock.x + 10.0f, y + 4.0f, dock.width - 20.0f, text("gui.empty"),
                   theme::textDim);
        return;
    }

    const int count = (int)canvas_->elements.size();
    const int capacity = std::max(1, (int)((dock.y + dock.height - y) / kRowStep));
    const int maxScroll = std::max(0, count - capacity);
    if (ui_.hovered(dock) && ui_.scrollDelta() != 0.0)
        hierarchyScroll_ = clampf(hierarchyScroll_ - (float)ui_.scrollDelta() * 2.0f, 0.0f,
                                  (float)maxScroll);

    for (int row = 0; row < capacity && row + (int)hierarchyScroll_ < count; ++row) {
        const int index = row + (int)hierarchyScroll_;
        const gui::Element* element = canvas_->at(index);
        if (!element) continue;

        const float rowY = y + (float)row * kRowStep;
        const Rect hit{dock.x + 4.0f, rowY, dock.width - 8.0f, kRowStep};
        const bool hovered = ui_.hovered(hit);
        const bool selected = index == selectedGui_;

        if (selected)
            ui.fillRect(hit.x, hit.y, hit.width, hit.height, theme::accentDim);
        else if (hovered)
            ui.fillRect(hit.x, hit.y, hit.width, hit.height, theme::header);

        // The depth is shown by the indent, the way a tree is shown.
        const float indent = 12.0f * (float)elementDepth(*canvas_, index);
        const float iconX = dock.x + 10.0f + indent;

        switch (element->kind) {
        case gui::ElementKind::Panel:
            ui.fillRect(iconX, rowY + 5.0f, 10.0f, 10.0f, theme::accentDim);
            break;
        case gui::ElementKind::Image:
            ui.fillRect(iconX, rowY + 5.0f, 10.0f, 10.0f, theme::success);
            break;
        case gui::ElementKind::Text:
            ui.fillRect(iconX, rowY + 7.0f, 10.0f, 6.0f, theme::textDim);
            break;
        case gui::ElementKind::Button:
            ui.fillRect(iconX, rowY + 5.0f, 10.0f, 10.0f, theme::warning);
            break;
        }

        // The name of the element, or its kind when it has no name: a row with
        // nothing in it is a row nobody can pick out of a list.
        const char* label =
            element->name.empty() ? kindLabel(element->kind) : element->name.c_str();
        ui.textFit(dock.x + 28.0f + indent, rowY + 2.0f, dock.width - 40.0f - indent, label,
                   element->visible ? theme::text : theme::textDim);

        if (hovered) ui_.markHovered(kGuiRow + index);
        if (hovered && ui_.pointerPressed()) {
            selectedGui_ = index;
            selectedBodies_.clear();
            activeBody_ = -1;
        }
    }
}

// The fields of the selected element, in the inspector's place.
void Editor::drawGuiInspector(UiRenderer& ui, Window& window, const Rect& dock, float& y) {
    if (!canvas_ || selectedGui_ < 0) return;
    gui::Element* element = canvas_->at(selectedGui_);
    if (!element) return;

    // The element as it was before the fields below are drawn, so that a
    // change can be taken back.
    const gui::Element before = *element;

    // --- the name ---
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("gui.element"));
    y += 26.0f;

    // The field follows the element, and not the other way round: a field that
    // was loaded once and never again would show the name of the element that
    // was selected when it was loaded - and an undo that moves an element
    // moves it under the field.
    if (guiNameFieldFor_ != selectedGui_ ||
        (!guiNameField_.editing && guiNameField_.text() != element->name)) {
        guiNameField_.set(element->name.c_str());
        guiNameField_.id = kGuiNameField;
        guiNameFieldFor_ = selectedGui_;
    }
    const Rect nameRect{dock.x + 8.0f, y, dock.width - 16.0f, theme::fieldHeight};
    ui_.pumpTextField(window, guiNameField_, ui_.hovered(nameRect));
    ui.fillRect(nameRect.x, nameRect.y, nameRect.width, nameRect.height,
                guiNameField_.editing ? theme::accent.scaled(0.35f) : theme::field);
    ui.textFit(nameRect.x + 4.0f,
               nameRect.y + (nameRect.height - theme::textLineHeight) * 0.5f,
               nameRect.width - 8.0f,
               guiNameField_.empty() ? text("gui.namePlaceholder") : guiNameField_.text(),
               guiNameField_.empty() ? theme::textDim : theme::text);
    // What was typed is the name now.
    element->name = guiNameField_.text();
    y += theme::fieldHeight + 6.0f;

    // --- the kind ---
    {
        char kindBuffer[32];
        std::snprintf(kindBuffer, sizeof(kindBuffer), "%s", kindLabel(element->kind));
        ui.text(dock.x + 10.0f, y, kindBuffer, theme::textDim);
        y += kRowStep;
    }

    // --- the rectangle ---
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("gui.rect"));
    y += 26.0f;
    ui_.propertyFloat(ui, kGuiProperty + 0, dock.x, y, dock.width, text("gui.posX"),
                      element->x, 1.0f, -99999.0f, 99999.0f);
    y += kRowStep;
    ui_.propertyFloat(ui, kGuiProperty + 1, dock.x, y, dock.width, text("gui.posY"),
                      element->y, 1.0f, -99999.0f, 99999.0f);
    y += kRowStep;
    ui_.propertyFloat(ui, kGuiProperty + 2, dock.x, y, dock.width, text("gui.width"),
                      element->width, 1.0f, 1.0f, 99999.0f);
    y += kRowStep;
    ui_.propertyFloat(ui, kGuiProperty + 3, dock.x, y, dock.width, text("gui.height"),
                      element->height, 1.0f, 1.0f, 99999.0f);
    y += kRowStep + 4.0f;

    // --- the letters ---
    if (element->kind == gui::ElementKind::Text ||
        element->kind == gui::ElementKind::Button) {
        ui_.sectionHeader(ui, dock.x, y, dock.width, text("gui.text"));
        y += 26.0f;

        if (guiTextFieldFor_ != selectedGui_ ||
            (!guiTextField_.editing && guiTextField_.text() != element->text)) {
            guiTextField_.set(element->text.c_str());
            guiTextField_.id = kGuiTextField;
            guiTextFieldFor_ = selectedGui_;
        }
        const Rect textRect{dock.x + 8.0f, y, dock.width - 16.0f, theme::fieldHeight};
        ui_.pumpTextField(window, guiTextField_, ui_.hovered(textRect));
        ui.fillRect(textRect.x, textRect.y, textRect.width, textRect.height,
                    guiTextField_.editing ? theme::accent.scaled(0.35f) : theme::field);
        ui.textFit(textRect.x + 4.0f,
                   textRect.y + (textRect.height - theme::textLineHeight) * 0.5f,
                   textRect.width - 8.0f,
                   guiTextField_.empty() ? text("gui.textPlaceholder") : guiTextField_.text(),
                   guiTextField_.empty() ? theme::textDim : theme::text);
        element->text = guiTextField_.text();
        y += theme::fieldHeight + 4.0f;

        ui_.propertyFloat(ui, kGuiProperty + 4, dock.x, y, dock.width, text("gui.fontSize"),
                          element->fontSize, 0.5f, 6.0f, 200.0f);
        y += kRowStep + 4.0f;
    }

    // --- the colour ---
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("gui.color"));
    y += 26.0f;
    ui_.propertyFine(ui, kGuiProperty + 5, dock.x, y, dock.width, text("gui.red"),
                     element->color.r, 0.002f, 0.0f, 1.0f);
    y += kRowStep;
    ui_.propertyFine(ui, kGuiProperty + 6, dock.x, y, dock.width, text("gui.green"),
                     element->color.g, 0.002f, 0.0f, 1.0f);
    y += kRowStep;
    ui_.propertyFine(ui, kGuiProperty + 7, dock.x, y, dock.width, text("gui.blue"),
                     element->color.b, 0.002f, 0.0f, 1.0f);
    y += kRowStep;
    ui_.propertyFine(ui, kGuiProperty + 8, dock.x, y, dock.width, text("gui.alpha"),
                     element->color.a, 0.002f, 0.0f, 1.0f);
    y += kRowStep + 4.0f;

    // --- the picture ---
    if (element->kind == gui::ElementKind::Image) {
        ui_.sectionHeader(ui, dock.x, y, dock.width, text("gui.texture"));
        y += 26.0f;
        if (guiTextureFieldFor_ != selectedGui_ ||
            (!guiTextureField_.editing && guiTextureField_.text() != element->texture)) {
            guiTextureField_.set(element->texture.c_str());
            guiTextureField_.id = kGuiTextureField;
            guiTextureFieldFor_ = selectedGui_;
        }
        const Rect textureRect{dock.x + 8.0f, y, dock.width - 16.0f, theme::fieldHeight};
        ui_.pumpTextField(window, guiTextureField_, ui_.hovered(textureRect));
        ui.fillRect(textureRect.x, textureRect.y, textureRect.width, textureRect.height,
                    guiTextureField_.editing ? theme::accent.scaled(0.35f) : theme::field);
        ui.textFit(textureRect.x + 4.0f,
                   textureRect.y + (textureRect.height - theme::textLineHeight) * 0.5f,
                   textureRect.width - 8.0f,
                   guiTextureField_.empty() ? text("gui.texturePlaceholder")
                                            : guiTextureField_.text(),
                   guiTextureField_.empty() ? theme::textDim : theme::text);
        element->texture = guiTextureField_.text();
        y += theme::fieldHeight + 4.0f;
    }

    // --- the flags ---
    ui_.sectionHeader(ui, dock.x, y, dock.width, text("gui.flags"));
    y += 26.0f;
    ui_.checkbox(ui, kGuiProperty + 9, dock.x + 8.0f, y, dock.width - 16.0f, text("gui.visible"),
                 element->visible);
    y += kRowStep;
    if (element->kind == gui::ElementKind::Button) {
        ui_.checkbox(ui, kGuiProperty + 10, dock.x + 8.0f, y, dock.width - 16.0f,
                     text("gui.interactable"), element->interactable);
        y += kRowStep;
    }
    y += 4.0f;

    // What the fields changed is a change that can be taken back, and it is
    // a change to the scene: the interface is written into the scene file.
    if (!sameElement(before, *element)) {
        markSceneDirty();
        // The change begins here: the element as it was before the fields
        // were drawn is what the change goes back to. A change that is already
        // being made - a drag in the viewport - keeps the state it began
        // with: a drag of a slider is one change and not one per frame.
        if (!guiBeforeValid_) {
            guiBefore_ = before;
            guiBeforeIndex_ = selectedGui_;
            guiBeforeValid_ = true;
        }
    }
}

// A new element of this kind, in the middle of the canvas, and selected.
int Editor::createGuiElement(gui::ElementKind kind) {
    if (!canvas_) return -1;

    gui::Element element;
    element.kind = kind;
    switch (kind) {
        case gui::ElementKind::Panel:
            element.name = "Panel";
            element.width = 320.0f;
            element.height = 180.0f;
            element.color = Color{0.16f, 0.18f, 0.22f, 1.0f};
            break;
        case gui::ElementKind::Image:
            element.name = "Image";
            element.width = 128.0f;
            element.height = 128.0f;
            element.color = Color{1.0f, 1.0f, 1.0f, 1.0f};
            break;
        case gui::ElementKind::Text:
            element.name = "Text";
            element.width = 240.0f;
            element.height = 32.0f;
            element.text = "Text";
            element.fontSize = 24.0f;
            break;
        case gui::ElementKind::Button:
            element.name = "Button";
            element.width = 180.0f;
            element.height = 44.0f;
            element.text = "Button";
            element.fontSize = 22.0f;
            element.color = Color{0.22f, 0.26f, 0.34f, 1.0f};
            break;
    }

    // In the middle of the canvas, so it is where the eye already is.
    element.x = (canvas_->width - element.width) * 0.5f;
    element.y = (canvas_->height - element.height) * 0.5f;

    const int index = canvas_->add(element);
    selectedGui_ = index;
    // A new element is one entry on the undo stack: Ctrl+Z takes it away.
    GuiUndoStep step;
    step.kind = GuiUndoStep::Kind::Added;
    step.index = index;
    step.element = element;
    guiUndo_.push_back(step);
    guiRedo_.clear();
    markSceneDirty();
    lastChange_ = LastChange::Gui;
    return index;
}

void Editor::deleteGuiElement() {
    if (!canvas_ || selectedGui_ < 0) return;
    const gui::Element* element = canvas_->at(selectedGui_);
    if (!element) return;
    GuiUndoStep step;
    step.kind = GuiUndoStep::Kind::Removed;
    step.index = selectedGui_;
    step.element = *element;
    canvas_->remove(selectedGui_);
    selectedGui_ = -1;
    guiUndo_.push_back(step);
    guiRedo_.clear();
    markSceneDirty();
    lastChange_ = LastChange::Gui;
}

// Remembers an element as it is, so that a change to it can be taken back.
void Editor::rememberGui(int index, const gui::Element& element) {
    if (!canvas_) return;
    if (index < 0 || index >= (int)canvas_->elements.size()) return;
    GuiUndoStep step;
    step.kind = GuiUndoStep::Kind::Changed;
    step.index = index;
    step.element = element;
    guiUndo_.push_back(step);
    guiRedo_.clear();
    lastChange_ = LastChange::Gui;
    // A hundred changes is more than anybody undoes, and a canvas is small.
    if (guiUndo_.size() > 100) guiUndo_.erase(guiUndo_.begin());
}

// Ends the change that was being made, if it is over.
//
// The same rule as the bodies: a change is over when the element stops
// changing. A drag of a slider leaves the element different from where it
// started, and it is the difference between one frame and the next that says
// whether anything is still moving.
void Editor::finishGuiUndoIfDone() {
    if (!canvas_ || !guiBeforeValid_) return;
    if (guiBeforeIndex_ < 0 || guiBeforeIndex_ >= (int)canvas_->elements.size()) {
        guiBeforeValid_ = false;
        return;
    }
    const gui::Element* now = canvas_->at(guiBeforeIndex_);
    if (!now) {
        guiBeforeValid_ = false;
        return;
    }

    if (guiSnapshotValid_ && guiSnapshotIndex_ == guiBeforeIndex_) {
        // Still moving: the element is not what it was at the end of the last
        // frame.
        if (!sameElement(*now, guiSnapshot_)) return;
    }
    // Either it stopped moving, or the selection moved away from it - and a
    // thing that is not on the screen any more is not moving either.

    if (!sameElement(*now, guiBefore_)) rememberGui(guiBeforeIndex_, guiBefore_);
    guiBeforeValid_ = false;
}

// Takes the last change to the interface back.
void Editor::undoGui() {
    if (!canvas_ || guiUndo_.empty()) return;
    const GuiUndoStep step = guiUndo_.back();
    guiUndo_.pop_back();

    switch (step.kind) {
    case GuiUndoStep::Kind::Changed:
        if (step.index < 0 || step.index >= (int)canvas_->elements.size()) break;
        *canvas_->at(step.index) = step.element;
        selectedGui_ = step.index;
        break;
    case GuiUndoStep::Kind::Added:
        if (step.index < 0 || step.index >= (int)canvas_->elements.size()) break;
        canvas_->remove(step.index);
        selectedGui_ = -1;
        break;
    case GuiUndoStep::Kind::Removed:
        // It goes back where it was, at the index it was at.
        if (step.index < 0 || step.index > (int)canvas_->elements.size()) break;
        canvas_->elements.insert(canvas_->elements.begin() + step.index, step.element);
        selectedGui_ = step.index;
        break;
    }

    guiRedo_.push_back(step);
    markSceneDirty();
    lastChange_ = LastChange::Gui;
}

// Puts the last change back the other way.
void Editor::redoGui() {
    if (!canvas_ || guiRedo_.empty()) return;
    const GuiUndoStep step = guiRedo_.back();
    guiRedo_.pop_back();

    switch (step.kind) {
    case GuiUndoStep::Kind::Changed:
        if (step.index < 0 || step.index >= (int)canvas_->elements.size()) break;
        *canvas_->at(step.index) = step.element;
        selectedGui_ = step.index;
        break;
    case GuiUndoStep::Kind::Added:
        if (step.index < 0 || step.index > (int)canvas_->elements.size()) break;
        canvas_->elements.insert(canvas_->elements.begin() + step.index, step.element);
        selectedGui_ = step.index;
        break;
    case GuiUndoStep::Kind::Removed:
        if (step.index < 0 || step.index >= (int)canvas_->elements.size()) break;
        canvas_->remove(step.index);
        selectedGui_ = -1;
        break;
    }

    guiUndo_.push_back(step);
    markSceneDirty();
    lastChange_ = LastChange::Gui;
}

} // namespace s3d
