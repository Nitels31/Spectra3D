#include "../Editor.hpp"

#include "../../core/Log.hpp"
#include "../../render/GlInfo.hpp"
#include "../../ui/Theme.hpp"

#include "../../i18n/Localizer.hpp"

namespace s3d {
namespace {

struct MenuEntry {
    const char* key;
    int id;
};

const MenuEntry kMenuEntries[5] = {{"menu.file", 0},  {"menu.edit", 1}, {"menu.view", 2},
                                  {"menu.create", 3}, {"menu.help", 4}};

} // namespace

void Editor::drawMenuBar(UiRenderer& ui, PhysicsWorld& world, int fbWidth) {
    const Rect bar = layout_.menuBar;
    ui.fillRect(bar.x, bar.y, bar.width, bar.height, theme::background);
    ui.fillRect(bar.x, bar.y + bar.height - 1.0f, bar.width, 1.0f, theme::separator);

    float x = 8.0f;
    for (const MenuEntry& entry : kMenuEntries) {
        const char* label = text(entry.key);
        const float textWidth = ui.textWidth(label);
        if (ui_.hovered(Rect{x, bar.y, textWidth + 16.0f, bar.height}))
            ui.fillRect(x, bar.y + 1.0f, textWidth + 16.0f, bar.height - 2.0f,
                        theme::buttonHover);
        ui.text(x + 8.0f, bar.y + 5.0f, label, theme::text);
        x += textWidth + 16.0f;
    }

    // Transport controls, centred like a media player.
    constexpr float kButtonWidth = 72.0f, kButtonGap = 4.0f, kButtonHeight = 22.0f;
    float cx = (float)fbWidth * 0.5f - 115.0f;
    if (ui_.button(ui, 0, cx, bar.y + 2.0f, kButtonWidth, kButtonHeight, text("btn.play"),
                   playMode_ && playing_)) {
        if (!playMode_) enterPlayMode(world);
        else playing_ = true;
    }
    cx += kButtonWidth + kButtonGap;
    if (ui_.button(ui, 1, cx, bar.y + 2.0f, kButtonWidth, kButtonHeight, text("btn.pause"),
                   playMode_ && !playing_)) {
        if (playMode_) {
            playing_ = !playing_;
            Log::info(playing_ ? text("log.resumed") : text("log.paused_"));
        }
    }
    cx += kButtonWidth + kButtonGap;
    if (ui_.button(ui, 2, cx, bar.y + 2.0f, kButtonWidth, kButtonHeight, text("btn.stop"))) {
        if (playMode_) exitPlayMode(world);
    }

    // Language toggle.
    const bool ukrainian = loc_.language() == i18n::Language::Ukrainian;
    if (ui_.button(ui, 3, (float)fbWidth - 120.0f, bar.y + 2.0f, 40.0f, kButtonHeight,
                   ukrainian ? "EN" : "UA"))
        loc_.setLanguage(ukrainian ? i18n::Language::English : i18n::Language::Ukrainian);

    // The real context version, not a hardcoded one: the engine asks for 3.3
    // compatibility and falls back to 2.1 on drivers that cannot give it, and the
    // label has to tell the truth either way.
    std::string glLabel = "GL ";
    glLabel += gl::requestedVersion();
    ui.text((float)fbWidth - 20.0f - ui.textWidth(glLabel.c_str()), bar.y + 5.0f, glLabel.c_str(),
            theme::textDim);
}

} // namespace s3d