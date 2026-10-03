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

    void begin(int width, int height);
    void end();

    void fillRect(float x, float y, float w, float h, const Color& color);
    void fillCircle(float cx, float cy, float radius, const Color& color);
    void text(float x, float y, const char* utf8, const Color& color);

private:
    TextAtlas atlas_;
    std::vector<float> colorBatch_;
    std::vector<float> textBatch_;
    int width_ = 0, height_ = 0;
};

} // namespace s3d