#include "../Editor.hpp"

#include "../../core/Log.hpp"
#include "../../core/Project.hpp"
#include "../../render/GlInfo.hpp"
#include "../../scene/BodyFactory.hpp"
#include "../../ui/Theme.hpp"

#include "../../i18n/Localizer.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace s3d {
namespace {

constexpr float kBarHeight = 28.0f;
constexpr float kMenuHeight = 22.0f;
constexpr float kTransportWidth = 78.0f;
constexpr float kTransportGap = 4.0f;
constexpr float kDropdownRow = 22.0f;

struct MenuItem {
    const char* key;
};

// The top bar's own menus. "Create" is the only one that opens something: it is
// where the shape palette went when the floating palette in the left column was
// removed, which is also where a user looks for "add a box".
const MenuItem kMenus[5] = {{"menu.file"}, {"menu.edit"}, {"menu.view"}, {"menu.create"},
                            {"menu.help"}};

// The only menu that opens anything.
constexpr int kCreateMenu = 3;

struct ShapeItem {
    Shape shape;
    const char* label;
};

const ShapeItem kShapes[7] = {
    {Shape::Box, "Box"},         {Shape::Sphere, "Sphere"},   {Shape::Cylinder, "Cylinder"},
    {Shape::Capsule, "Capsule"}, {Shape::Cone, "Cone"},       {Shape::Pyramid, "Pyramid"},
    {Shape::Prism, "Prism"},
};

} // namespace

void Editor::drawTopBar(UiRenderer& ui, PhysicsWorld& world, Camera& camera, int fbWidth) {
    const Rect bar = layout_.menuBar;
    ui.fillRect(bar.x, bar.y, bar.width, bar.height, theme::background);
    ui.fillRect(bar.x, bar.y + bar.height - 1.0f, bar.width, 1.0f, theme::separator);

    menuOpenedThisFrame_ = false;

    // --- left: the menus ---
    float x = 6.0f;
    for (int i = 0; i < 5; ++i) {
        const char* label = text(kMenus[i].key);
        const float width = ui.textWidth(label) + 16.0f;
        const Rect hit{x, bar.y + 2.0f, width, kMenuHeight};
        const bool hovered = ui_.hovered(hit);
        if (hovered) ui.fillRect(hit.x, hit.y, hit.width, hit.height, theme::buttonHover);
        ui.text(hit.x + 8.0f, hit.y + 4.0f, label, theme::text);

        // Only Create opens; the rest are labels until they have something to do, and
        // a menu that swallows clicks without effect is worse than a label.
        // Closing on an outside click belongs to the dropdown, which knows its
        // own rectangle - doing it here closed the menu before the row under the
        // pointer ever saw the press, so picking a shape did nothing.
        if (i == kCreateMenu && ui_.pointerPressed() && hovered) {
            openMenu_ = openMenu_ == kCreateMenu ? -1 : kCreateMenu;
            // The press that opens a menu must not also be the press that closes
            // it, and no state may change after this frame has been drawn - with
            // on-demand rendering a change nobody redraws stays on screen forever.
            menuOpenedThisFrame_ = true;
        }
        x += width;
    }

    // --- centre: transport, like a media player ---
    const float groupWidth = kTransportWidth * 3.0f + kTransportGap * 2.0f;
    float tx = (float)fbWidth * 0.5f - groupWidth * 0.5f;
    auto transport = [&](int id, const char* label, bool on) {
        const Rect rect{tx, bar.y + 3.0f, kTransportWidth, kMenuHeight};
        const bool pressed = ui_.button(ui, id, rect.x, rect.y, rect.width, rect.height, label, on);
        tx += kTransportWidth + kTransportGap;
        return pressed;
    };
    if (transport(kTransportPlay, text("btn.play"), playMode_ && playing_)) {
        if (!playMode_) enterPlayMode(world);
        else playing_ = true;
    }
    if (transport(kTransportPause, text("btn.pause"), playMode_ && !playing_)) {
        if (playMode_) {
            playing_ = !playing_;
            Log::info(playing_ ? text("log.resumed") : text("log.paused_"));
        }
    }
    if (transport(kTransportStop, text("btn.stop"), false)) {
        if (playMode_) exitPlayMode(world);
    }

    // --- right: GL version, language, then engine and project ---
    std::string glLabel = "GL ";
    glLabel += gl::requestedVersion();
    const float glWidth = ui.textWidth(glLabel.c_str());
    const float right = (float)fbWidth - 14.0f;
    ui.text(right - glWidth, bar.y + 7.0f, glLabel.c_str(), theme::textDim);

    const bool ukrainian = loc_.language() == i18n::Language::Ukrainian;
    const Rect language{right - glWidth - 44.0f, bar.y + 3.0f, 38.0f, kMenuHeight};
    if (ui_.button(ui, kTransportLang, language.x, language.y, language.width, language.height,
                   ukrainian ? "EN" : "UA"))
        loc_.setLanguage(ukrainian ? i18n::Language::English : i18n::Language::Ukrainian);

    if (!projectName_.empty()) {
        std::string title = kEngineName;
        title += " - ";
        title += projectName_;
        const float full = ui.textWidth(title.c_str());
        const float room = language.x - x - 20.0f;
        if (room > 40.0f) {
            // Shrink rather than clip: which project this is, is the one thing in
            // this bar that cannot be guessed.
            const float scale = std::fmin(1.0f, room / full);
            ui.textScaled(language.x - 12.0f - full * scale, bar.y + 7.0f, title.c_str(),
                          theme::textDim, scale);
        }
    }
}

// The Create dropdown, drawn as an overlay at the very end of the frame. It hangs
// over the scene header and the tool column, so anything drawn after the top bar
// would paint over it.
void Editor::drawCreateMenu(UiRenderer& ui, PhysicsWorld& world, Camera& camera) {
    if (openMenu_ != kCreateMenu) return;

    // Anchored under the Create label, which is the fourth one.
    float createX = 6.0f;
    for (int i = 0; i < 3; ++i) createX += ui.textWidth(text(kMenus[i].key)) + 16.0f;
    const Rect panel{createX, layout_.menuBar.y + kBarHeight - 1.0f, 150.0f,
                     kDropdownRow * (float)std::size(kShapes) + 6.0f};
    ui.fillRect(panel.x, panel.y, panel.width, panel.height, theme::panel);
    ui.fillRect(panel.x, panel.y, panel.width, 1.0f, theme::separator);

    for (int i = 0; i < (int)std::size(kShapes); ++i) {
        const Rect row{panel.x + 3.0f, panel.y + 3.0f + (float)i * kDropdownRow,
                       panel.width - 6.0f, kDropdownRow};
        const bool hovered = ui_.hovered(row);
        if (hovered) ui.fillRect(row.x, row.y, row.width, row.height, theme::buttonHover);
        ui.text(row.x + 8.0f, row.y + 4.0f, kShapes[i].label, theme::text);
        if (hovered && ui_.pointerPressed()) {
            BodyFactory::spawnInFrontOf(world, kShapes[i].shape, camera, 1.0f);
            openMenu_ = -1;
        }
    }

    // A press outside the panel closes the menu without picking - unless it is the
    // press that opened it, or the row under the pointer, which is handled above.
    const bool onRow = ui_.hovered(panel);
    if (ui_.pointerPressed() && !onRow && !menuOpenedThisFrame_) openMenu_ = -1;
}
} // namespace s3d