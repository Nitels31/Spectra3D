#include "gui/GuiRenderer.hpp"

#include "core/Log.hpp"

#include <algorithm>
#include <cmath>

namespace s3d {
namespace gui {
namespace {

// How much a button changes when the pointer is on it and when it is pressed.
// Two numbers and not a palette: a button that is drawn with a colour of its
// own still has to say that it can be pressed.
constexpr float kHoverBrightness = 1.18f;
constexpr float kPressedBrightness = 0.72f;

} // namespace

bool Renderer::buildFonts() { return fonts_.open(); }

void Renderer::begin(int width, int height, const Rect& area) {
    width_ = width;
    height_ = height;
    solidBatch_.clear();
    imageBatch_.clear();
    textBatch_.clear();
    imageTexture_ = 0;

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    // The origin at the top left, y downwards: the same space every other
    // rectangle in the engine is in, so a canvas does not have to be flipped
    // to be put where it belongs.
    glOrtho(0.0, (double)width, (double)height, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // The canvas is cut to the rectangle it is drawn into. A picture that
    // hangs over the edge of the viewport would be drawn over the panel
    // beside it, and the panel is not part of the game.
    if (area.width > 0.0f && area.height > 0.0f) {
        const int x0 = (int)std::floor(area.x);
        const int y0 = (int)std::floor(area.y);
        const int x1 = (int)std::ceil(area.x + area.width);
        const int y1 = (int)std::ceil(area.y + area.height);
        glEnable(GL_SCISSOR_TEST);
        glScissor(x0, height_ - y1, x1 - x0, y1 - y0);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
}

void Renderer::end() {
    flush();
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glViewport(0, 0, width_, height_);
}

void Renderer::flush() {
    flushSolids();
    flushImages();
    flushText();
}

void Renderer::flushSolids() {
    if (solidBatch_.empty()) return;
    glDisable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(2, GL_FLOAT, 24, solidBatch_.data());
    glColorPointer(4, GL_FLOAT, 24, solidBatch_.data() + 2);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(solidBatch_.size() / 6));
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    solidBatch_.clear();
}

void Renderer::flushImages() {
    if (imageBatch_.empty()) return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, imageTexture_);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(2, GL_FLOAT, 32, imageBatch_.data());
    glTexCoordPointer(2, GL_FLOAT, 32, imageBatch_.data() + 2);
    glColorPointer(4, GL_FLOAT, 32, imageBatch_.data() + 4);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(imageBatch_.size() / 8));
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    imageBatch_.clear();
    imageTexture_ = 0;
}

void Renderer::flushText() {
    if (textBatch_.empty() || textTexture_ == 0) {
        textBatch_.clear();
        return;
    }
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, textTexture_);
    // The colour of the letters is in the batch, and the batch holds three
    // numbers of colour to a vertex. The transparency is not one of them, and
    // it comes from here: a letter that is half transparent is a letter nobody
    // can read.
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(2, GL_FLOAT, 28, textBatch_.data());
    glTexCoordPointer(2, GL_FLOAT, 28, textBatch_.data() + 2);
    glColorPointer(3, GL_FLOAT, 28, textBatch_.data() + 4);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(textBatch_.size() / 7));
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    textBatch_.clear();
}

void Renderer::fillRect(float x, float y, float w, float h, const Color& color) {
    // The letters are drawn over the rectangles and the pictures are drawn
    // over the rectangles, so a rectangle that comes after them has to be
    // drawn after them as well - or it would land underneath.
    flushText();
    flushImages();

    const size_t base = solidBatch_.size();
    solidBatch_.resize(base + 36);
    float* out = solidBatch_.data() + base;
    const float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    const float vx[6] = {x0, x1, x1, x0, x1, x0};
    const float vy[6] = {y0, y0, y1, y0, y1, y1};
    for (int i = 0; i < 6; ++i) {
        *out++ = vx[i];
        *out++ = vy[i];
        *out++ = color.r;
        *out++ = color.g;
        *out++ = color.b;
        *out++ = color.a;
    }
}

void Renderer::image(unsigned texture, float x, float y, float w, float h, const Color& color) {
    if (!texture) {
        fillRect(x, y, w, h, color);
        return;
    }
    flushText();
    // One draw call per picture: a batch with two pictures in it would have to
    // be split anyway, and the split is here rather than in the middle of the
    // geometry.
    if (!imageBatch_.empty() && imageTexture_ != texture) flushImages();
    imageTexture_ = texture;

    const size_t base = imageBatch_.size();
    imageBatch_.resize(base + 48);
    float* out = imageBatch_.data() + base;
    const float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    const float vx[6] = {x0, x1, x1, x0, x1, x0};
    const float vy[6] = {y0, y0, y1, y0, y1, y1};
    const float vu[6] = {0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f};
    const float vv[6] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f};
    for (int i = 0; i < 6; ++i) {
        *out++ = vx[i];
        *out++ = vy[i];
        *out++ = vu[i];
        *out++ = vv[i];
        *out++ = color.r;
        *out++ = color.g;
        *out++ = color.b;
        *out++ = color.a;
    }
}

void Renderer::text(float x, float y, const char* utf8, const Color& color, float size) {
    if (!utf8 || !*utf8) return;
    // The letters are over the rectangles and over the pictures, so what is in
    // the batches has to be drawn before them.
    flushSolids();
    flushImages();
    // The letters of a size are in a picture of their own. A picture that
    // changes is a batch that has to be drawn before the change, and a picture
    // that is let go of cannot be drawn from - so the picture of the size that
    // is asked for is taken first, and the batch is flushed when it is not the
    // one the batch was drawn with.
    const unsigned texture = fonts_.textureFor(size);
    if (texture != textTexture_) {
        flushText();
        textTexture_ = texture;
    }
    fonts_.appendQuads(textBatch_, x, y, utf8, color.r, color.g, color.b, size);
}

float Renderer::textWidth(const char* utf8, float fontSize) {
    if (!utf8 || !*utf8) return 0.0f;
    return fonts_.measure(utf8, fontSize);
}

void Renderer::draw(Canvas& canvas, const Rect& area, TextureRegistry& textures,
                    bool interactive, float pointerX, float pointerY, bool pointerDown,
                    bool pointerPressed, bool pointerReleased) {
    if (area.width < 1.0f || area.height < 1.0f) return;

    // The button the press started on. A click is a press and a release on the
    // SAME button: a press on one button and a release on another is not a
    // click on either of them, and a press that started outside every button
    // is not a click at all.
    if (pointerPressed) pressedButton_ = interactive ? buttonAt(canvas, area, pointerX, pointerY) : -1;

    for (size_t i = 0; i < canvas.elements.size(); ++i)
        drawElement(canvas, (int)i, area, textures, interactive, pointerX, pointerY, pointerDown,
                    pointerReleased);

    // The press is over: the next press starts a new one.
    if (pointerReleased) pressedButton_ = -1;
}

// The button under the pointer, or -1. The one on top wins, and a button that
// cannot be pressed is not under the pointer at all.
int Renderer::buttonAt(Canvas& canvas, const Rect& area, float px, float py) const {
    int found = -1;
    for (size_t i = 0; i < canvas.elements.size(); ++i) {
        const Element& element = canvas.elements[i];
        if (!element.visible) continue;
        if (element.kind != ElementKind::Button || !element.interactable) continue;
        if (!canvas.screenRect((int)i, area).contains(px, py)) continue;
        found = (int)i;
    }
    return found;
}

void Renderer::drawElement(Canvas& canvas, int index, const Rect& area,
                           TextureRegistry& textures, bool interactive, float pointerX,
                           float pointerY, bool pointerDown, bool pointerReleased) {
    Element* element = canvas.at(index);
    if (!element) return;

    // What the pointer did to it is what happened this frame, not what it was:
    // a button that was pressed a frame ago and is not pressed now is not
    // pressed.
    element->hovered = false;
    element->pressed = false;
    element->clicked = false;
    if (!element->visible) return;

    const Rect rect = canvas.screenRect(index, area);

    const bool button = element->kind == ElementKind::Button;
    if (button && interactive && element->interactable) {
        element->hovered = rect.contains(pointerX, pointerY);
        element->pressed = (pressedButton_ == index) && pointerDown;
        element->clicked = (pressedButton_ == index) && pointerReleased;
    }

    float scale = 1.0f, offsetX = 0.0f, offsetY = 0.0f;
    canvas.fit(area, scale, offsetX, offsetY);
    // The letters are drawn at the size they are drawn at, in the pixels of the
    // screen. A letter taken out of a bitmap of another size is a soft letter:
    // the shape is right and the edges are not, and the interface of a game
    // has letters of every size in it.
    const float textSize = element->fontSize * scale;

    switch (element->kind) {
    case ElementKind::Panel:
        fillRect(rect.x, rect.y, rect.width, rect.height, element->color);
        break;

    case ElementKind::Image: {
        if (!element->texture.empty()) {
            const std::string path = resolveTexturePath(element->texture);
            int index2 = textures.find(path);
            if (index2 < 0) {
                // The picture is loaded once and then it is there: the registry
                // keeps every texture for the whole session.
                index2 = textures.loadFile(path, /*quiet=*/true);
                if (index2 < 0)
                    Log::warn("GUI: the picture '%s' could not be loaded", element->texture.c_str());
            }
            if (index2 >= 0) {
                image(textures.at(index2), rect.x, rect.y, rect.width, rect.height, element->color);
                break;
            }
        }
        // No picture: a coloured rectangle, which is what a panel is.
        fillRect(rect.x, rect.y, rect.width, rect.height, element->color);
        break;
    }

    case ElementKind::Text: {
        if (element->text.empty()) break;
        text(rect.x, rect.y, element->text.c_str(), element->textColor, textSize);
        break;
    }

    case ElementKind::Button: {
        Color background = element->color;
        if (element->pressed) background = background.scaled(kPressedBrightness);
        else if (element->hovered) background = background.scaled(kHoverBrightness);
        fillRect(rect.x, rect.y, rect.width, rect.height, background);

        if (element->text.empty()) break;
        // The letters are in the middle of the button, both ways.
        const float lineHeight = fonts_.lineHeightFor(textSize);
        const float width = fonts_.measure(element->text.c_str(), textSize);
        text(rect.x + (rect.width - width) * 0.5f, rect.y + (rect.height - lineHeight) * 0.5f,
             element->text.c_str(), element->textColor, textSize);
        break;
    }
    }
}

} // namespace gui
} // namespace s3d
