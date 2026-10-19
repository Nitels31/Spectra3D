#include "editor/Editor.hpp"

#include "ui/Theme.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace s3d {
namespace {

constexpr float kHeaderHeight = 24.0f;
constexpr float kRowStep = theme::rowHeight;
// The two buttons at the end of a row: play the sound, give it to the body
// that is selected.
constexpr float kButtonSize = 18.0f;
constexpr float kButtonGap = 4.0f;

} // namespace

// The Sounds panel: the sounds the engine has loaded, and what can be done
// with them.
//
// Two buttons in every row, because there are two things one wants to do with
// a sound: hear it, and give it to a body so that the body makes it when it
// hits something. Hearing it is a click on the speaker; giving it to a body
// is a click on the box, and it needs a body to be selected - a button that
// does nothing when there is nothing to do it to is a button that lies.
void Editor::drawSoundsDock(UiRenderer& ui, PhysicsWorld& world, const Rect& dock) {
    if (dock.width <= 0.0f || dock.height <= 0.0f) return;

    const char* title = text("tab.sounds");
    ui.fillRect(dock.x, dock.y, dock.width, kHeaderHeight, theme::panelDark);
    ui.fillRect(dock.x, dock.y + kHeaderHeight - 1.0f, dock.width, 1.0f, theme::separator);
    ui.text(dock.x + 18.0f, dock.y + 5.0f, title, theme::text);

    const std::vector<std::string>& names = sound_.names();

    // The device. "No device" is the answer on a machine with no sound card,
    // and it is a different answer from "no sounds loaded" - one of them is a
    // problem with the machine and the other is a problem with the folder.
    //
    // The name is cut short: a device's own name can be a paragraph long
    // ("Speakers (2- High Definition Audio Device)"), and the panel's X is in
    // the way of the end of it. The full name is in the log.
    std::string device = sound_.running() ? sound_.deviceName() : text("sounds.noDevice");
    const size_t bracket = device.find('(');
    if (bracket != std::string::npos) device = device.substr(0, bracket);
    while (!device.empty() && device.back() == ' ') device.pop_back();
    ui.textFit(dock.x + 26.0f + ui.textWidth(title), dock.y + 5.0f, dock.width - 110.0f,
               device.c_str(), sound_.running() ? theme::textDim : theme::warning);

    if (names.empty()) {
        const char* empty = text("sounds.empty");
        const char* hint = text("sounds.hint");
        const float y = dock.y + kHeaderHeight + 12.0f;
        ui.textFit(dock.x + 10.0f, y, dock.width - 20.0f, empty, theme::textDim);
        ui.textFit(dock.x + 10.0f, y + theme::textLineHeight + 6.0f, dock.width - 20.0f, hint,
                   theme::textDim.scaled(0.85f));
        return;
    }

    const float rowsTop = dock.y + kHeaderHeight + 4.0f;
    const int capacity = std::max(1, (int)((dock.y + dock.height - rowsTop) / kRowStep));

    for (int row = 0; row < capacity && row < (int)names.size(); ++row) {
        const std::string& name = names[(size_t)row];
        const float y = rowsTop + (float)row * kRowStep;
        const Rect hit{dock.x + 4.0f, y, dock.width - 8.0f, kRowStep};
        const bool hovered = ui_.hovered(hit);
        if (hovered) ui.fillRect(hit.x, hit.y, hit.width, hit.height, theme::header);

        // The name, and whether it is playing right now: a sound that is
        // playing is drawn in the accent colour, so the panel says what is
        // sounding without anybody having to hear it.
        const bool playing = sound_.isPlaying(name);
        ui.textFit(dock.x + 12.0f, y + 2.0f, dock.width - 70.0f, name.c_str(),
                   playing ? theme::accent : theme::text);

        // Play.
        const Rect play{dock.x + dock.width - 62.0f, y + 3.0f, kButtonSize, kButtonSize};
        if (ui_.button(ui, kSoundRow + row * 4, play.x, play.y, play.width, play.height,
                       ">", playing))
            sound_.play(name);

        // Give it to the body that is selected.
        const Rect assign{dock.x + dock.width - 40.0f, y + 3.0f, kButtonSize, kButtonSize};
        const bool canAssign = activeBody_ >= 0 && activeBody_ < (int)world.bodies.size();
        if (ui_.button(ui, kSoundRow + row * 4 + 1, assign.x, assign.y, assign.width,
                       assign.height, "+", false) &&
            canAssign) {
            world.bodies[(size_t)activeBody_].sound = name;
            markSceneDirty();
        }
    }
}

} // namespace s3d
