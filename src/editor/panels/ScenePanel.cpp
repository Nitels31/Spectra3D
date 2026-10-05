#include "../Editor.hpp"

#include "../../ui/Theme.hpp"

#include <algorithm>

namespace s3d {
namespace {

constexpr float kHeaderHeight = 26.0f;
constexpr float kToolSize = 26.0f;
constexpr float kToolGap = 3.0f;
constexpr float kNavBallInset = 52.0f;
constexpr float kBarSize = 20.0f;
constexpr float kBarGap = 3.0f;

// Icons of the viewport toolbar, drawn from rects and circles because that is all
// the UI renderer has. Each one is a different shape on purpose: letters on a
// toolbar say nothing about what the button does and look the same whether it is
// active or not.
enum class ViewIcon { Grid, Vectors, Snap, Frame, Reset, Layers };

void drawViewIcon(UiRenderer& ui, const Rect& r, ViewIcon icon, const Color& tint) {
    const float cx = r.x + r.width * 0.5f;
    const float cy = r.y + r.height * 0.5f;
    const float s = std::fmin(r.width, r.height);
    auto bar = [&](float x, float y, float w, float h) { ui.fillRect(x, y, w, h, tint); };

    switch (icon) {
    case ViewIcon::Grid:
        // Three lines each way: what it toggles looks like this from above.
        for (int i = -1; i <= 1; ++i) {
            bar(cx - 6.0f, cy + (float)i * 4.0f - 0.5f, 12.0f, 1.0f);
            bar(cx + (float)i * 4.0f - 0.5f, cy - 6.0f, 1.0f, 12.0f);
        }
        break;
    case ViewIcon::Vectors:
        bar(cx - 5.0f, cy - 0.5f, 9.0f, 1.5f);
        bar(cx + 3.0f, cy - 3.0f, 1.5f, 6.0f);
        bar(cx + 1.5f, cy - 3.0f, 1.5f, 2.0f);
        bar(cx + 1.5f, cy + 1.0f, 1.5f, 2.0f);
        break;
    case ViewIcon::Snap:
        // A magnet: two legs and a crossbar. The one icon in the row whose shape
        // is a thing rather than a mark, which is why it is the one that has to be
        // drawn - there is no glyph for it in a font either.
        bar(cx - 5.0f, cy - 5.0f, 1.5f, 7.0f);
        bar(cx + 3.5f, cy - 5.0f, 1.5f, 7.0f);
        bar(cx - 5.0f, cy + 1.0f, 10.0f, 1.5f);
        break;
    case ViewIcon::Frame: {
        // Four corner brackets: "put the selection in frame".
        const float arm = 4.0f, off = 6.0f;
        bar(cx - off, cy - off, arm, 1.0f);
        bar(cx - off, cy - off, 1.0f, arm);
        bar(cx + off - arm, cy - off, arm, 1.0f);
        bar(cx + off - 1.0f, cy - off, 1.0f, arm);
        bar(cx - off, cy + off - 1.0f, arm, 1.0f);
        bar(cx - off, cy + off - arm, 1.0f, arm);
        bar(cx + off - arm, cy + off - 1.0f, arm, 1.0f);
        bar(cx + off - 1.0f, cy + off - arm, 1.0f, arm);
        break;
    }
    case ViewIcon::Reset: {
        // A ring with a gap at the top and a head in it.
        const float radius = s * 0.26f;
        for (int i = 0; i < 12; ++i) {
            const float a = (float)i / 12.0f * 2.0f * kPi + kPi * 0.15f;
            ui.fillCircle(cx + std::cos(a) * radius, cy + std::sin(a) * radius, s * 0.055f,
                          tint);
        }
        for (int i = 0; i < 5; ++i) {
            const float t = (float)i / 4.0f;
            const float w = s * 0.30f * (1.0f - std::fabs(t * 2.0f - 1.0f));
            bar(cx - s * 0.15f, cy - radius - s * 0.22f + (float)i * (s * 0.07f), w, s * 0.08f);
        }
        break;
    }
    case ViewIcon::Layers:
        // Three plates: the panel it opens is a list of them.
        for (int i = 0; i < 3; ++i)
            bar(cx - 6.0f, cy - 4.0f + (float)i * 3.0f, 12.0f - (float)i * 2.0f, 1.5f);
        break;
    }
}

const float kPositiveOffsets[3][2] = {{26.0f, 7.0f}, {0.0f, -26.0f}, {-22.0f, 22.0f}};
const float kNegativeOffsets[3][2] = {{-26.0f, -7.0f}, {0.0f, 26.0f}, {22.0f, -22.0f}};
const Color kAxisColors[3] = {{0.90f, 0.27f, 0.27f}, {0.55f, 0.90f, 0.27f},
                              {0.27f, 0.52f, 0.90f}};
const char* const kAxisLabels[3] = {"X", "Y", "Z"};

} // namespace

void Editor::drawNavigationBalls(UiRenderer& ui) const {
    const Rect viewport = layout_.viewport;
    const float cx = viewport.x + viewport.width - kNavBallInset;
    const float cy = viewport.y + kNavBallInset;

    // Ball first, then the coloured ends on top of it: a ring drawn afterwards
    // would cut the handles off.
    ui.fillCircle(cx, cy, 42.0f, Color{0.10f, 0.12f, 0.16f, 0.55f});
    for (int i = 0; i < 3; ++i) {
        ui.fillCircle(cx + kNegativeOffsets[i][0], cy + kNegativeOffsets[i][1], 7.0f,
                      kAxisColors[i].scaled(0.35f));
    }
    for (int i = 0; i < 3; ++i) {
        const float px = cx + kPositiveOffsets[i][0], py = cy + kPositiveOffsets[i][1];
        ui.fillCircle(px, py, 9.0f, kAxisColors[i]);
        ui.text(px - ui.textWidth(kAxisLabels[i]) * 0.5f, py - 6.0f, kAxisLabels[i],
                Color{1, 1, 1});
    }
    ui.fillCircle(cx, cy, 5.0f, Color{0.92f, 0.92f, 0.95f, 0.95f});
}

// "Scene" strip across the top of the viewport.
//
// The tools used to be duplicated here as letters, which meant two places to
// change a tool and no way to tell which buttons were active. The tool column and
// the top bar own them now, so this strip is the viewport toolbar from the sketch:
// a row of square buttons on the right, each one a command that does something.
void Editor::drawSceneHeader(UiRenderer& ui, PhysicsWorld& world, Camera& camera,
                             Window& window) {
    const Rect header = layout_.sceneHeader;
    ui.fillRect(header.x, header.y, header.width, header.height, theme::panelDark);
    ui.fillRect(header.x, header.y + header.height - 1.0f, header.width, 1.0f,
                theme::separator);
    ui.text(header.x + 8.0f, header.y + 6.0f, text("tab.scene"), theme::text);

    // Laid out from the right edge inwards, so the row can never run into the
    // title on a narrow window.
    float x = header.x + header.width - 8.0f;
    auto button = [&](int id, float size) {
        const Rect rect{x - size, header.y + 3.0f, size, size};
        x -= size + kBarGap;
        return rect;
    };

    // Layers: the same panel the top bar opens, from here too.
    {
        const Rect rect = button(kSceneHeader + 40, kBarSize);
        if (ui_.button(ui, kSceneHeader + 40, rect.x, rect.y, rect.width, rect.height, "",
                       layersOpen_))
            layersOpen_ = !layersOpen_;
        drawViewIcon(ui, rect, ViewIcon::Layers, layersOpen_ ? theme::accent : theme::textDim);
    }
    {
        const Rect rect = button(kSceneHeader + 41, kBarSize);
        if (ui_.button(ui, kSceneHeader + 41, rect.x, rect.y, rect.width, rect.height, ""))
            runAction(Action::ResetCamera, 0, world, camera, window);
        drawViewIcon(ui, rect, ViewIcon::Reset, theme::textDim);
    }
    {
        const Rect rect = button(kSceneHeader + 42, kBarSize);
        if (ui_.button(ui, kSceneHeader + 42, rect.x, rect.y, rect.width, rect.height, ""))
            frameSelection(world, camera);
        drawViewIcon(ui, rect, ViewIcon::Frame, theme::textDim);
    }

    {
        const Rect rect = button(kSceneHeader + 44, kBarSize);
        if (ui_.button(ui, kSceneHeader + 44, rect.x, rect.y, rect.width, rect.height, "",
                       snapEnabled_))
            snapEnabled_ = !snapEnabled_;
        drawViewIcon(ui, rect, ViewIcon::Snap, snapEnabled_ ? theme::accent : theme::textDim);
    }
    {
        const Rect rect = button(kSceneHeader + 45, kBarSize);
        if (ui_.button(ui, kSceneHeader + 45, rect.x, rect.y, rect.width, rect.height, "",
                       showVectors_))
            showVectors_ = !showVectors_;
        drawViewIcon(ui, rect, ViewIcon::Vectors, showVectors_ ? theme::accent : theme::textDim);
    }
    {
        const Rect rect = button(kSceneHeader + 46, kBarSize);
        if (ui_.button(ui, kSceneHeader + 46, rect.x, rect.y, rect.width, rect.height, "",
                       showGrid_))
            showGrid_ = !showGrid_;
        drawViewIcon(ui, rect, ViewIcon::Grid, showGrid_ ? theme::accent : theme::textDim);
    }
}

// Tool icons, drawn from rects and circles because that is all the UI renderer
// has. Each one is a different shape on purpose: the previous toolbar used the
// letters G/M/S/#, which said nothing about what the tool did and looked the same
// whether it was active or not.
void Editor::drawToolIcon(UiRenderer& ui, const Rect& r, Tool tool, bool on) const {
    const Color tint = on ? theme::accent : theme::textDim;
    const float cx = r.x + r.width * 0.5f;
    const float cy = r.y + r.height * 0.5f;
    const float s = std::fmin(r.width, r.height);

    auto bar = [&](float x, float y, float w, float h) { ui.fillRect(x, y, w, h, tint); };
    auto dot = [&](float x, float y, float radius) { ui.fillCircle(x, y, radius, tint); };

    switch (tool) {
    case Tool::Select: {
        // Cursor arrow: a triangle narrowing to a point, built from stacked rows.
        const float w = s * 0.42f, h = s * 0.62f;
        const int rows = 7;
        for (int i = 0; i < rows; ++i) {
            const float t = (float)i / (float)(rows - 1);
            bar(cx - w * 0.5f, cy - h * 0.5f + h * t, w * (1.0f - t), h / (float)rows + 0.5f);
        }
        break;
    }
    case Tool::Move: {
        // Four-way arrow: a cross with a head on each end.
        const float arm = s * 0.30f, thick = s * 0.10f, head = s * 0.16f;
        bar(cx - arm, cy - thick * 0.5f, arm * 2.0f, thick);
        bar(cx - thick * 0.5f, cy - arm, thick, arm * 2.0f);
        bar(cx - head, cy - arm - head * 0.6f, head * 2.0f, head * 0.6f);
        bar(cx - head, cy + arm, head * 2.0f, head * 0.6f);
        bar(cx - arm - head * 0.6f, cy - head, head * 0.6f, head * 2.0f);
        bar(cx + arm, cy - head, head * 0.6f, head * 2.0f);
        break;
    }
    case Tool::Rotate: {
        // A broken ring with an arrow head on the gap, so it cannot be mistaken
        // for the scale tool's square or the pan tool's fingers.
        const float radius = s * 0.30f;
        const int steps = 14;
        for (int i = 0; i < steps; ++i) {
            const float a = (float)i * (2.0f * kPi / (float)steps);
            dot(cx + std::cos(a) * radius, cy + std::sin(a) * radius, s * 0.055f);
        }
        const float tip = (float)steps * (2.0f * kPi / (float)steps);
        bar(cx + std::cos(tip) * radius - s * 0.10f, cy + std::sin(tip) * radius - s * 0.05f,
            s * 0.20f, s * 0.10f);
        break;
    }
    case Tool::Scale: {
        // Nested squares growing to the upper right, the usual scale metaphor.
        const float o = s * 0.34f, t = s * 0.07f;
        bar(cx - o, cy - o, o * 2.0f, t);
        bar(cx - o, cy + o - t, o * 2.0f, t);
        bar(cx - o, cy - o, t, o * 2.0f);
        bar(cx + o - t, cy - o, t, o * 2.0f);
        const float i2 = o * 0.45f;
        bar(cx - i2, cy - i2, i2 * 2.0f, t * 0.8f);
        bar(cx - i2, cy + i2 - t * 0.8f, i2 * 2.0f, t * 0.8f);
        bar(cx - i2, cy - i2, t * 0.8f, i2 * 2.0f);
        bar(cx + i2 - t * 0.8f, cy - i2, t * 0.8f, i2 * 2.0f);
        break;
    }
    case Tool::Pan: {
        // An open hand: four fingers over a palm.
        const float w = s * 0.36f;
        const float finger = s * 0.075f;
        for (int i = 0; i < 4; ++i) {
            const float x = cx - w * 0.5f + w * (float)i / 3.0f - finger * 0.5f;
            // The middle fingers are the tall ones, which is what makes it read as
            // a hand rather than as a bar chart.
            const float tall = (i == 1 || i == 2) ? s * 0.30f : s * 0.20f;
            bar(x, cy - tall, finger, tall + s * 0.10f);
        }
        bar(cx - w * 0.5f - finger * 0.3f, cy + s * 0.10f, w + finger * 0.6f, s * 0.13f);
        break;
    }
    }
}

// Vertical tool column down the viewport's left edge: it is where the pointer
// already is when it is inside the scene.
void Editor::drawViewportTools(UiRenderer& ui, Camera& camera) {
    const Rect strip = layout_.viewportTools;
    float y = strip.y;

    auto tool = [&](int id, Tool value) {
        const Rect rect{strip.x, y, kToolSize, kToolSize};
        const bool on = tool_ == value;
        const bool pressed = ui_.button(ui, id, rect.x, rect.y, rect.width, rect.height, "", on);
        drawToolIcon(ui, rect, value, on);
        y += kToolSize + kToolGap;
        return pressed;
    };

    if (tool(kSceneHeader + 10, Tool::Select)) tool_ = Tool::Select;
    if (tool(kSceneHeader + 11, Tool::Move)) tool_ = Tool::Move;
    if (tool(kSceneHeader + 12, Tool::Rotate)) tool_ = Tool::Rotate;
    if (tool(kSceneHeader + 13, Tool::Scale)) tool_ = Tool::Scale;
    if (tool(kSceneHeader + 14, Tool::Pan)) tool_ = Tool::Pan;

    drawNavigationBalls(ui);
    (void)camera;
}

} // namespace s3d