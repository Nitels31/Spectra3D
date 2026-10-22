#pragma once
#include <string>
#include <vector>

#include "render/Camera.hpp"  // Rect
#include "render/Color.hpp"

namespace s3d {
namespace gui {

// THE INTERFACE OF A GAME, AS NUMBERS.
//
// This is the layer Unity calls "uGUI": a canvas with things on it - a panel, a
// picture, a line of letters, a button - and a place in the editor where
// those things are put where they belong.
//
// The canvas lives in the scene, not in the editor: the interface of a game is
// part of the game, and a game that is built has to carry its interface with
// it. So the canvas is written into the scene file, read back when the scene
// is opened, and drawn by the player program exactly as the editor draws it.
//
// The layer is below `scene` and above `render`: the canvas needs colours and
// rectangles from the render layer, and the scene needs the canvas to write it
// down.

// The kinds of thing a canvas can hold.
//
// A panel is a coloured rectangle. An image is a rectangle with a picture in
// it. A text is a line of letters in a box. A button is a panel with a line
// of letters in it that answers the pointer.
enum class ElementKind { Panel = 0, Image, Text, Button };

// The name a scene file gives a kind, and the other direction.
//
// The names are the ones a person would write, and the file is meant to be
// read by a person: "panel", "image", "text", "button".
const char* elementKindName(ElementKind kind);
bool elementKindFromName(const std::string& name, ElementKind& out);

// One piece of a canvas.
//
// The rectangle is in the canvas's own units: pixels of the resolution the
// canvas was laid out for, the origin at the top left, y downwards - the way
// a picture is measured. The renderer scales it to whatever it is drawn on, so
// a layout made for 1920x1080 is the same layout on a smaller screen.
//
// `parent` is the index of the element this one sits in, or -1 for the canvas
// itself. A child's rectangle is measured from its parent's top left corner,
// so a panel that moves takes its children with it.
struct Element {
    ElementKind kind = ElementKind::Panel;
    // The name a script asks for. It does not have to be unique, but a script
    // that asks for a name finds the first element that carries it.
    std::string name;
    int parent = -1;

    float x = 0.0f, y = 0.0f, width = 160.0f, height = 40.0f;
    // The colour of a panel, of an image, or of a button's background.
    Color color{0.16f, 0.18f, 0.22f, 1.0f};
    // The colour of the letters.
    Color textColor{1.0f, 1.0f, 1.0f, 1.0f};
    // The letters of a text or of a button.
    std::string text;
    // The picture of an image, as a path relative to the assets folder.
    // Empty for none: an image without a picture is a coloured rectangle, which
    // is what a panel is.
    std::string texture;
    // The size of the letters, in the canvas's own units.
    float fontSize = 24.0f;
    bool visible = true;
    // Whether a button answers the pointer. A button that does not is drawn
    // like one that does and does nothing when it is pressed.
    bool interactable = true;

    // What the pointer did to it this frame. Filled in by the renderer and
    // never written to a scene file: it is what happened, not what was made.
    bool hovered = false;
    bool pressed = false;
    bool clicked = false;
};

// The canvas: the interface of a game, as numbers.
//
// The size is the resolution the layout was made for. The renderer scales it
// to the rectangle it is drawn into, keeping the aspect, so the layout is the
// same layout on every screen and nothing is a different size on a different
// machine.
struct Canvas {
    float width = 1920.0f;
    float height = 1080.0f;
    std::vector<Element> elements;

    // The first element with this name, or -1.
    int find(const std::string& name) const;
    // The element at this index, or nullptr.
    Element* at(int index);
    const Element* at(int index) const;
    // The rectangle of an element in the canvas's own units, with the
    // offsets of its parents added in.
    Rect absoluteRect(int index) const;
    // The scale and the offset that turn the canvas's units into the pixels
    // of a rectangle on the screen. The canvas is centred in it.
    void fit(const Rect& area, float& scale, float& offsetX, float& offsetY) const;
    // The rectangle of an element on the screen, in window coordinates.
    Rect screenRect(int index, const Rect& area) const;
    // The element under a point of the screen, or -1. The one on top wins:
    // the last one drawn over the point. An element that cannot be seen is
    // not under the pointer.
    int pickAt(const Rect& area, float px, float py) const;
    // Adds an element and hands back its index.
    int add(const Element& element);
    // Removes an element and takes its children with it. The children go
    // first, so the parents that are left keep pointing at things that are
    // still there.
    void remove(int index);
    // True when the element is inside another one: a child cannot be its own
    // parent, and a cycle would make the offsets grow without end.
    bool wouldCycle(int parent, int child) const;
};

// The file a picture name points at.
//
// The name in a scene is relative to the assets folder - "textures/button.png"
// - so that it means the same thing on every machine. The project's own assets
// win and the engine's are the fallback, the same as every other kind of
// asset.
std::string resolveTexturePath(const std::string& relative);

} // namespace gui
} // namespace s3d
