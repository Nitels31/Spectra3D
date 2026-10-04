#pragma once
#include <vector>

#include <GL/gl.h>

#include "Camera.hpp"
#include "Color.hpp"
#include "TextAtlas.hpp"

namespace s3d {

// Batched 2D renderer. Every widget in the editor submits geometry through this
// class between begin() and end(); the whole frame becomes two draw calls.
class UiRenderer {
public:
bool buildFonts();
    bool fontsReady() const { return atlas_.ready(); }
    float textWidth(const char* utf8) const { return atlas_.textWidth(utf8); }
    // Width of `utf8` drawn at `scale`. Linear in the scale, so a caller can solve
    // for the scale that fits a given space without a second pass over the string.
    float textWidthScaled(const char* utf8, float scale) const {
        return atlas_.textWidth(utf8) * (scale > 0.0f ? scale : 1.0f);
    }
    // Smallest factor text is allowed to shrink to before it is cut instead.
    static constexpr float kMinTextScale = 0.6f;

    void begin(int width, int height);
    void end();
    // Draws everything queued so far and starts a new batch. An overlay needs
    // this: the batcher emits every rectangle before any text, so without a flush
    // an overlay's background lands UNDER the text of the panel it is covering.
    void flush();

    void fillRect(float x, float y, float w, float h, const Color& color);
    void fillCircle(float cx, float cy, float radius, const Color& color);
    void text(float x, float y, const char* utf8, const Color& color);
    // Same glyphs at `scale`. Used to fit a long string into a narrow slot instead
    // of chopping characters off it.
    void textScaled(float x, float y, const char* utf8, const Color& color, float scale);
    // Draws `utf8` inside `maxWidth`, shrinking it until it fits and never going
    // below kMinTextScale. Returns the scale used, so a caller can put a caret or
    // a second column right after the text.
    float textFit(float x, float y, float maxWidth, const char* utf8, const Color& color);

private:
    TextAtlas atlas_;
    std::vector<float> colorBatch_;
    std::vector<float> textBatch_;
    int width_ = 0, height_ = 0;
};

} // namespace s3d