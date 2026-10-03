#include "../Editor.hpp"

#include "../../core/Log.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>

namespace s3d {
namespace {

const float kPositiveOffsets[3][2] = {{26.0f, 7.0f}, {0.0f, -26.0f}, {-22.0f, 22.0f}};
const float kNegativeOffsets[3][2] = {{-26.0f, -7.0f}, {0.0f, 26.0f}, {22.0f, -22.0f}};
const Color kAxisColors[3] = {{0.90f, 0.27f, 0.27f}, {0.55f, 0.90f, 0.27f},
                              {0.27f, 0.52f, 0.90f}};
const char* const kAxisLabels[3] = {"X", "Y", "Z"};

} // namespace

void Editor::drawNavigationBalls(UiRenderer& ui) const {
    const Rect viewport = layout_.viewport;
    const float cx = viewport.x + viewport.width - 52.0f;
    const float cy = viewport.y + 52.0f;

    ui.fillCircle(cx, cy, 44.0f, Color{0.10f, 0.12f, 0.16f, 0.55f});
    for (int i = 0; i < 3; ++i) {
        const float nx = cx + kNegativeOffsets[i][0], ny = cy + kNegativeOffsets[i][1];
        const float px = cx + kPositiveOffsets[i][0], py = cy + kPositiveOffsets[i][1];
        ui.fillCircle(nx, ny, 7.0f, kAxisColors[i].scaled(0.35f));
        ui.fillCircle(px, py, 9.0f, kAxisColors[i]);
        const float textWidth = ui.textWidth(kAxisLabels[i]);
        ui.text(px - textWidth * 0.5f, py - 6.0f, kAxisLabels[i], Color{1, 1, 1});
    }
    ui.fillCircle(cx, cy, 5.0f, Color{0.92f, 0.92f, 0.95f, 0.95f});
}

void Editor::drawViewportChrome(UiRenderer& ui, Camera& camera) {
    const Rect chrome = layout_.viewportChrome;
    ui.fillRect(chrome.x, chrome.y, chrome.width, chrome.height, theme::panelDark);
    ui.fillRect(chrome.x, chrome.y + chrome.height - 1.0f, chrome.width, 1.0f,
                theme::separator);

    float x = chrome.x + 6.0f;
    const float y = chrome.y + 2.0f;
    constexpr float kGap = 6.0f, kHeight = 22.0f;

    auto adaptiveButton = [&](int id, const char* label, bool toggled,
                              float minWidth) -> bool {
        const float width = clampf(ui.textWidth(label) + 18.0f, minWidth, 96.0f);
        const bool pressed = ui_.button(ui, id, x, y, width, kHeight, label, toggled);
        x += width + kGap;
        return pressed;
    };

    if (adaptiveButton(kViewportChrome, "GIZ", gizmoEnabled_, 42.0f))
        gizmoEnabled_ = !gizmoEnabled_;
    if (adaptiveButton(kViewportChrome + 1, "MOV",
                      gizmoEnabled_ && gizmoMode_ == gizmo::Mode::Move, 42.0f)) {
        gizmoMode_ = gizmo::Mode::Move;
        gizmoEnabled_ = true;
    }
    if (adaptiveButton(kViewportChrome + 2, "SCL",
                      gizmoEnabled_ && gizmoMode_ == gizmo::Mode::Scale, 42.0f)) {
        gizmoMode_ = gizmo::Mode::Scale;
        gizmoEnabled_ = true;
    }

    ui.fillRect(x + 2.0f, y + 2.0f, 1.0f, 18.0f, theme::separator);
    x += 10.0f;

    if (adaptiveButton(kViewportChrome + 3, text("btn.vec"), showVectors_, 48.0f))
        showVectors_ = !showVectors_;
    if (adaptiveButton(kViewportChrome + 4, text("btn.grid"), showGrid_, 48.0f))
        showGrid_ = !showGrid_;

    // Field of view selector, right-aligned and clear of the navigation balls.
    static const char* const kPerspectiveKeys[3] = {"vp.perspective60", "vp.perspective70",
                                                    "vp.perspective90"};
    static const float kPerspectiveValues[3] = {60.0f, 70.0f, 90.0f};
    const char* perspectiveLabel = text(kPerspectiveKeys[perspectiveIndex_]);
    float width = clampf(ui.textWidth(perspectiveLabel) + 22.0f, 82.0f, 118.0f);
    constexpr float kNavBallReserve = 74.0f;
    float buttonX = chrome.x + chrome.width - width - kNavBallReserve;
    if (buttonX < x + 8.0f) {
        buttonX = x + 8.0f;
        if (buttonX + width + kNavBallReserve > chrome.x + chrome.width)
            width = chrome.x + chrome.width - buttonX - kNavBallReserve;
    }
    if (width >= 60.0f &&
        ui_.button(ui, kViewportChrome + 5, buttonX, y, width, kHeight, perspectiveLabel)) {
        perspectiveIndex_ = (perspectiveIndex_ + 1) % 3;
        camera.fovDeg = kPerspectiveValues[perspectiveIndex_];
    }

    drawNavigationBalls(ui);
}

void Editor::drawStatusBar(UiRenderer& ui, PhysicsWorld& world, int fbWidth, int fbHeight) {
    const Rect bar = layout_.statusBar;
    ui.fillRect(bar.x, bar.y, bar.width, bar.height, theme::background);
    ui.fillRect(bar.x, bar.y, bar.width, 1.0f, theme::separator);

    if (ui_.tab(ui, bar.x, bar.y - 24.0f, 100.0f, text("tab.console"), showConsole_))
        showConsole_ = !showConsole_;

    char status[128];
    const char* mode = playMode_ ? (playing_ ? text("bar.playing") : text("bar.paused"))
                                 : text("bar.edit");
    std::snprintf(status, sizeof(status), "%s  |  %s: %d  |  %s: %s", mode,
                  text("bar.bodies"), (int)world.bodies.size(), text("bar.gravity"),
                  gravityName(world.gravityAxis));
    ui.text(120.0f, bar.y + 6.0f, status, theme::textDim);
    ui.text((float)fbWidth - 90.0f, bar.y + 6.0f, "v2.0.0", theme::textDim);

    if (showConsole_) drawConsolePanel(ui, world, fbWidth);
}

void Editor::drawConsolePanel(UiRenderer& ui, PhysicsWorld& world, int fbWidth) {
    (void)world;
    constexpr float kConsoleHeight = 132.0f;
    const float consoleY = layout_.statusBar.y - kConsoleHeight;

    ui.fillRect(0.0f, consoleY, (float)fbWidth, kConsoleHeight,
                theme::panelDark.scaled(0.97f));
    ui.fillRect(0.0f, consoleY, (float)fbWidth, 1.0f, theme::separator);

    const std::vector<Log::Entry>& lines = Log::entries();
    const int shown = std::min<int>((int)lines.size(), 6);
    for (int i = 0; i < shown; ++i) {
        const Log::Entry& entry = lines[lines.size() - shown + i];
        Color color{0.62f, 0.65f, 0.70f};
        if (entry.level == Log::Level::Error) color = theme::error;
        else if (entry.level == Log::Level::Warn) color = theme::warning;
        ui.text(10.0f, consoleY + 8.0f + (float)i * 19.0f, entry.text, color);
    }
    if (lines.empty())
        ui.text(10.0f, consoleY + 8.0f, text("bar.consoleEmpty"), theme::textDim);
}

} // namespace s3d