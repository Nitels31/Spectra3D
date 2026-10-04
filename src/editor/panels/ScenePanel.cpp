#include "../Editor.hpp"

#include "../../ui/Theme.hpp"

#include <algorithm>

namespace s3d {
namespace {

constexpr float kHeaderHeight = 26.0f;
constexpr float kToolSize = 26.0f;
constexpr float kToolGap = 3.0f;
constexpr float kNavBallInset = 52.0f;

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

// "Scene" strip across the top of the viewport: title on the left, view options
// as square icon buttons on the right.
void Editor::drawSceneHeader(UiRenderer& ui, PhysicsWorld& world, Camera& camera) {
    const Rect header = layout_.sceneHeader;
    ui.fillRect(header.x, header.y, header.width, header.height, theme::panelDark);
    ui.fillRect(header.x, header.y + header.height - 1.0f, header.width, 1.0f,
                theme::separator);
    ui.text(header.x + 8.0f, header.y + 6.0f, text("tab.scene"), theme::text);

    // Laid out from the right edge inwards, so the row can never run into the
    // title on a narrow window.
    const float size = 20.0f;
    float x = header.x + header.width - 8.0f;

    auto icon = [&](int id, const char* glyph, bool on) {
        const Rect rect{x - size, header.y + 3.0f, size, size};
        const bool pressed = ui_.button(ui, id, rect.x, rect.y, rect.width, rect.height, "", on);
        ui.text(rect.x + (size - ui.textWidth(glyph)) * 0.5f,
                rect.y + (size - theme::textLineHeight) * 0.5f, glyph,
                on ? theme::accent : theme::textDim);
        x -= size + kToolGap;
        return pressed;
    };

    static const char* const kPerspectiveKeys[3] = {"vp.perspective60", "vp.perspective70",
                                                    "vp.perspective90"};
    static const float kPerspectiveValues[3] = {60.0f, 70.0f, 90.0f};
    const char* fovLabel = text(kPerspectiveKeys[perspectiveIndex_]);
    const float fovWidth = std::fmax(size, ui.textWidth(fovLabel) + 14.0f);
    if (ui_.button(ui, kSceneHeader + 40, x - fovWidth, header.y + 3.0f, fovWidth, size, fovLabel)) {
        perspectiveIndex_ = (perspectiveIndex_ + 1) % 3;
        camera.fovDeg = kPerspectiveValues[perspectiveIndex_];
    }
    x -= fovWidth + kToolGap;

    if (icon(kSceneHeader + 2, "S", gizmoEnabled_ && gizmoMode_ == gizmo::Mode::Scale))
        gizmoMode_ = gizmo::Mode::Scale;
    if (icon(kSceneHeader + 1, "M", gizmoEnabled_ && gizmoMode_ == gizmo::Mode::Move))
        gizmoMode_ = gizmo::Mode::Move;
    if (icon(kSceneHeader + 3, "V", showVectors_)) showVectors_ = !showVectors_;
    if (icon(kSceneHeader + 4, "#", showGrid_)) showGrid_ = !showGrid_;
    ui.fillRect(x, header.y + 6.0f, 1.0f, header.height - 12.0f, theme::separator);
    x -= kToolGap;
    if (icon(kSceneHeader + 0, "G", gizmoEnabled_)) gizmoEnabled_ = !gizmoEnabled_;
    (void)world;
}

// Vertical tool column down the viewport's left edge: it is where the pointer
// already is when it is inside the scene.
void Editor::drawViewportTools(UiRenderer& ui, Camera& camera) {
    const Rect strip = layout_.viewportTools;
    float y = strip.y;

    auto tool = [&](int id, const char* glyph, bool on) {
        const Rect rect{strip.x, y, kToolSize, kToolSize};
        const bool pressed = ui_.button(ui, id, rect.x, rect.y, rect.width, rect.height, "", on);
        ui.text(rect.x + (kToolSize - ui.textWidth(glyph)) * 0.5f,
                rect.y + (kToolSize - theme::textLineHeight) * 0.5f, glyph,
                on ? theme::accent : theme::textDim);
        y += kToolSize + kToolGap;
        return pressed;
    };

    if (tool(kSceneHeader + 10, "G", gizmoEnabled_)) gizmoEnabled_ = !gizmoEnabled_;
    if (tool(kSceneHeader + 11, "M", gizmoEnabled_ && gizmoMode_ == gizmo::Mode::Move)) {
        gizmoMode_ = gizmo::Mode::Move;
        gizmoEnabled_ = true;
    }
    if (tool(kSceneHeader + 12, "S", gizmoEnabled_ && gizmoMode_ == gizmo::Mode::Scale)) {
        gizmoMode_ = gizmo::Mode::Scale;
        gizmoEnabled_ = true;
    }
    ui.fillRect(strip.x + 2.0f, y + 1.0f, kToolSize - 4.0f, 1.0f, theme::separator);
    y += 6.0f;
    if (tool(kSceneHeader + 13, "#", showGrid_)) showGrid_ = !showGrid_;

    drawNavigationBalls(ui);
    (void)camera;
}

} // namespace s3d