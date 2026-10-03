#include "UiRenderer.hpp"

#include "../core/Math.hpp"

namespace s3d {
namespace {

constexpr int kCircleSegments = 20;

} // namespace

bool UiRenderer::buildFonts() { return atlas_.build(); }

void UiRenderer::begin(int width, int height) {
    width_ = width;
    height_ = height;
    colorBatch_.clear();
    textBatch_.clear();

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, (double)width, (double)height, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void UiRenderer::end() {
    if (!colorBatch_.empty()) {
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(2, GL_FLOAT, 24, colorBatch_.data());
        glColorPointer(4, GL_FLOAT, 24, colorBatch_.data() + 2);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(colorBatch_.size() / 6));
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
    }
    if (!textBatch_.empty() && atlas_.ready()) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, atlas_.texture());
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
    }

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

void UiRenderer::fillRect(float x, float y, float w, float h, const Color& color) {
    const float quad[6][6] = {{x, y, color.r, color.g, color.b, color.a},
                              {x + w, y, color.r, color.g, color.b, color.a},
                              {x + w, y + h, color.r, color.g, color.b, color.a},
                              {x, y, color.r, color.g, color.b, color.a},
                              {x + w, y + h, color.r, color.g, color.b, color.a},
                              {x, y + h, color.r, color.g, color.b, color.a}};
    for (const auto& vertex : quad)
        colorBatch_.insert(colorBatch_.end(), vertex, vertex + 6);
}

void UiRenderer::fillCircle(float cx, float cy, float radius, const Color& color) {
    for (int i = 0; i < kCircleSegments; ++i) {
        const float a0 = (float)i / kCircleSegments * 2.0f * kPi;
        const float a1 = (float)(i + 1) / kCircleSegments * 2.0f * kPi;
        const float triangle[3][6] = {
            {cx, cy, color.r, color.g, color.b, color.a},
            {cx + std::cos(a0) * radius, cy + std::sin(a0) * radius, color.r, color.g, color.b, color.a},
            {cx + std::cos(a1) * radius, cy + std::sin(a1) * radius, color.r, color.g, color.b, color.a}};
        for (const auto& vertex : triangle)
            colorBatch_.insert(colorBatch_.end(), vertex, vertex + 6);
    }
}

void UiRenderer::text(float x, float y, const char* utf8, const Color& color) {
    atlas_.appendQuads(textBatch_, x, y, utf8, color.r, color.g, color.b);
}

} // namespace s3d