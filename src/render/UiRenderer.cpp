#include "UiRenderer.hpp"

#include "../core/Math.hpp"

namespace s3d {
namespace {

constexpr int kCircleSegments = 20;
constexpr int kCircleTableSize = kCircleSegments + 1; // entry 20 closes the loop

// Unit-circle trigonometry is constant, so it is evaluated once instead of four
// times per circle segment. a1 of segment i is bit-identical to a0 of segment
// i + 1, so entries 1..20 hold both roles and the table reproduces exactly the
// values the old per-segment expressions produced.
struct CircleTable {
    float cos[kCircleTableSize];
    float sin[kCircleTableSize];
};

const CircleTable& unitCircle() {
    static const CircleTable table = [] {
        CircleTable t{};
        for (int i = 0; i < kCircleSegments; ++i) {
            const float a0 = (float)i / kCircleSegments * 2.0f * kPi;
            const float a1 = (float)(i + 1) / kCircleSegments * 2.0f * kPi;
            t.cos[i] = std::cos(a0);
            t.sin[i] = std::sin(a0);
            t.cos[i + 1] = std::cos(a1);
            t.sin[i + 1] = std::sin(a1);
        }
        return t;
    }();
    return table;
}

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
    // Same six vertices in the same order as before, written straight into the
    // batch: one grow instead of six vector::insert calls per rectangle.
    const size_t base = colorBatch_.size();
    colorBatch_.resize(base + 36);
    float* out = colorBatch_.data() + base;

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

void UiRenderer::fillCircle(float cx, float cy, float radius, const Color& color) {
    const CircleTable& circle = unitCircle();
    const float r = color.r, g = color.g, b = color.b, a = color.a;

    const size_t base = colorBatch_.size();
    colorBatch_.resize(base + (size_t)kCircleSegments * 18);
    float* out = colorBatch_.data() + base;
    for (int i = 0; i < kCircleSegments; ++i) {
        const float px[3] = {cx, cx + circle.cos[i] * radius,
                             cx + circle.cos[i + 1] * radius};
        const float py[3] = {cy, cy + circle.sin[i] * radius,
                             cy + circle.sin[i + 1] * radius};
        for (int k = 0; k < 3; ++k) {
            *out++ = px[k];
            *out++ = py[k];
            *out++ = r;
            *out++ = g;
            *out++ = b;
            *out++ = a;
        }
    }
}

void UiRenderer::text(float x, float y, const char* utf8, const Color& color) {
    atlas_.appendQuads(textBatch_, x, y, utf8, color.r, color.g, color.b);
}

void UiRenderer::textScaled(float x, float y, const char* utf8, const Color& color, float scale) {
    if (scale >= 1.0f) {
        atlas_.appendQuads(textBatch_, x, y, utf8, color.r, color.g, color.b);
        return;
    }
    atlas_.appendQuads(textBatch_, x, y, utf8, color.r, color.g, color.b, scale);
}

} // namespace s3d