#include "render/UiRenderer.hpp"

#include "core/Math.hpp"

#include <algorithm>

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
    clipDepth_ = 0;
    clipOverflow_ = 0;
    // The scissor test is state, and state survives a frame. Forgetting to turn it
    // off here meant a panel whose clip rectangle moved - a floating panel, a dock
    // dragged to the other edge - had the PREVIOUS frame's box still in force for
    // everything it drew before its own push, and silently drew nothing.
    glDisable(GL_SCISSOR_TEST);
    colorBatch_.clear();
    textBatch_.clear();
    // Reserve once so the per-rectangle and per-glyph grows below never hit the
    // geometric reallocation mid-frame. The capacities are generous enough for a
    // full editor frame and only ever grow.
    if (colorBatch_.capacity() < 24576) colorBatch_.reserve(24576);
    if (textBatch_.capacity() < 117504) textBatch_.reserve(117504);

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
    flush();

    // Symmetric with begin(): whatever a panel pushed, the next frame starts from
    // a clean state.
    clipDepth_ = 0;
    clipOverflow_ = 0;
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

void UiRenderer::flush() {
    if (!colorBatch_.empty()) {
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(2, GL_FLOAT, 24, colorBatch_.data());
        glColorPointer(4, GL_FLOAT, 24, colorBatch_.data() + 2);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(colorBatch_.size() / 6));
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
        colorBatch_.clear();
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
        textBatch_.clear();
    }
}

void UiRenderer::pushClip(const Rect& rect) {
    // Flush first: the batches hold geometry submitted under the previous clip,
    // and submitting it now would mix clipped and unclipped triangles into one
    // draw call.
    flush();
    if (clipDepth_ >= kMaxClips) {
        // Counted, not just refused. popClip() cannot tell an overflowing push from
        // a real one by depth alone, so a silent return here meant the matching pop
        // decremented the depth and restored somebody else's box: the stack came
        // unwound one entry per overflow while the panel still believed it was
        // nested, and everything after it was cut to the wrong rectangle. Nothing
        // in the editor nests eight deep today, which is why this never fired - but
        // it fails silently the day a panel does.
        ++clipOverflow_;
        return;
    }

    // Round OUT, not in. A scissor box has to CONTAIN the rectangle it is meant to
    // clip, so the left and top edges go down and the right and bottom go up:
    // `(int)rect.x` and `(int)rect.width` truncate, which threw away a pixel
    // whenever the bounds were fractional - and they are, because the inspector
    // scrolls by a float. Rect in window coordinates (y down) becomes GL
    // coordinates (y up) here, and `height_` is an integer, so
    // floor(height_ - v) == height_ - ceil(v); that identity is why the two edges
    // collapse into the four lines below instead of six.
    const int x0 = (int)std::floor(rect.x);
    const int y0 = (int)std::floor(rect.y);
    const int x1 = (int)std::ceil(rect.x + rect.width);
    const int y1 = (int)std::ceil(rect.y + rect.height);
    // GL scissors cannot be switched off per triangle, so an empty intersection is
    // expressed as a degenerate box - nothing inside it can pass.
    Clip next{x0, height_ - y1, x1 - x0, y1 - y0};
    if (clipDepth_ > 0) {
        const Clip& outer = clips_[clipDepth_ - 1];
        const int left = std::max(outer.x, next.x);
        const int right = std::min(outer.x + outer.width, next.x + next.width);
        const int top = std::max(outer.y, next.y);
        const int bottom = std::min(outer.y + outer.height, next.y + next.height);
        next = Clip{left, top, std::max(0, right - left), std::max(0, bottom - top)};
    }
    clips_[clipDepth_++] = next;
    glEnable(GL_SCISSOR_TEST);
    glScissor(next.x, next.y, next.width, next.height);
}

void UiRenderer::popClip() {
    if (clipDepth_ <= 0) return;
    // After the flush, before the decrement: an overflowing push left the scissor
    // exactly where it was, so the matching pop restores nothing and must not spend
    // a depth it never took.
    flush();
    if (clipOverflow_ > 0) {
        --clipOverflow_;
        return;
    }
    --clipDepth_;
    if (clipDepth_ == 0) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    const Clip& current = clips_[clipDepth_ - 1];
    glScissor(current.x, current.y, current.width, current.height);
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

float UiRenderer::textFit(float x, float y, float maxWidth, const char* utf8,
                          const Color& color) {
    if (!utf8 || !*utf8 || maxWidth <= 0.0f) return 1.0f;
    const float full = atlas_.textWidth(utf8);
    if (full <= maxWidth) {
        atlas_.appendQuads(textBatch_, x, y, utf8, color.r, color.g, color.b);
        return 1.0f;
    }
    // Width is linear in the scale, so one division is exact - no search loop.
    const float scale = std::max(kMinTextScale, maxWidth / full);
    atlas_.appendQuads(textBatch_, x, y, utf8, color.r, color.g, color.b, scale);
    return scale;
}

} // namespace s3d