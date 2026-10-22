#pragma once
#include <vector>

#include "assets/TextureRegistry.hpp"
#include "gui/GuiCanvas.hpp"
#include "render/Color.hpp"
#include "render/GlyphCache.hpp"

namespace s3d {
namespace gui {

// THE INTERFACE OF A GAME, DRAWN.
//
// A canvas is a picture in numbers, and this is what draws it: over the world,
// in the window's own coordinates, with the letters baked at the size they are
// drawn at - the letters of the editor's own interface are one size, and the
// letters of a game are every size its author asks for.
//
// Three batches, because there are three kinds of thing to draw: a flat
// colour, a picture, and a line of letters. The order they are drawn in is the
// order of the canvas - a panel under a label, a label under a panel - and a
// batch is flushed when the next thing would have to be drawn over it, so the
// canvas reads the way it was laid out.
//
// The editor draws the same canvas the player program does, and the only
// difference between the two is whether the buttons answer the pointer: in the
// editor a click picks an element, and in a game a click presses a button.
class Renderer {
public:
    bool buildFonts();
    bool fontsReady() const { return fonts_.ready(); }

    // The frame: the window, and the rectangle the canvas is drawn into.
    // Everything is in window coordinates, the origin at the top left.
    void begin(int width, int height, const Rect& area);
    void end();

    // Draws the canvas into `area`. `interactive` says whether the buttons
    // answer the pointer; the pointer's own state is passed in, because the
    // renderer asks the window nothing.
    void draw(Canvas& canvas, const Rect& area, TextureRegistry& textures,
              bool interactive, float pointerX, float pointerY, bool pointerDown,
              bool pointerPressed, bool pointerReleased);

    // Draws whatever is left over. The editor draws the selection handles
    // after the canvas, and they have to be over it.
    void flush();

    // The width of a line of text at a given size, in the canvas's own units.
    // The inspector measures a label with it so that the box it is in is the
    // box the text needs.
    float textWidth(const char* utf8, float fontSize);

private:
    void drawElement(Canvas& canvas, int index, const Rect& area, TextureRegistry& textures,
                     bool interactive, float pointerX, float pointerY, bool pointerDown,
                     bool pointerReleased);
    // The button under the pointer, or -1.
    int buttonAt(Canvas& canvas, const Rect& area, float px, float py) const;
    void fillRect(float x, float y, float w, float h, const Color& color);
    void image(unsigned texture, float x, float y, float w, float h, const Color& color);
    // Draws a line of letters at `size` pixels - the size they are drawn at,
    // not the size they are written at.
    void text(float x, float y, const char* utf8, const Color& color, float size);

    void flushSolids();
    void flushImages();
    void flushText();

    GlyphCache fonts_;
    // x, y, r, g, b, a - six floats to a vertex.
    std::vector<float> solidBatch_;
    // x, y, u, v, r, g, b, a - eight floats to a vertex.
    std::vector<float> imageBatch_;
    // x, y, u, v, r, g, b - seven floats to a vertex.
    std::vector<float> textBatch_;
    // The picture the letters are drawn with. The letters of a size are in a
    // picture of their own, and a batch of letters is flushed when the picture
    // changes: one picture, one draw call.
    unsigned textTexture_ = 0;
    // The picture the image batch is drawn with. A batch is flushed when the
    // picture changes: one draw call per picture, and never a batch of two.
    unsigned imageTexture_ = 0;
    // The button the press started on, or -1. A click is a press and a release
    // on the same one.
    int pressedButton_ = -1;
    int width_ = 0, height_ = 0;
};

} // namespace gui
} // namespace s3d
