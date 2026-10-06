#include "editor/Editor.hpp"

#include "core/Log.hpp"
#include "ui/Theme.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace s3d {
namespace {

constexpr float kHeaderHeight = 24.0f;
constexpr float kLineHeight = theme::textLineHeight + 3.0f;

struct LevelFilter {
    bool* flag;
    int count;
    const char* label;
    Color color;
};

} // namespace

// Console. Docked along the bottom or floating over the scene, and it does not
// care: the rectangle it is handed is the rectangle it fills. The header carries
// one filter per level with a live count: a quiet log is the normal state, and
// "Errors 3" is the one thing in it worth reading.
void Editor::drawConsolePanel(UiRenderer& ui, const Rect& panel) {
    if (panel.width <= 0.0f || panel.height <= 0.0f) return;

    ui.fillRect(panel.x, panel.y, panel.width, panel.height, theme::panelDark.scaled(0.97f));
    ui.fillRect(panel.x, panel.y, panel.width, 1.0f, theme::separator);
    ui.fillRect(panel.x, panel.y, panel.width, kHeaderHeight, theme::panelDark);
    ui.fillRect(panel.x, panel.y + kHeaderHeight - 1.0f, panel.width, 1.0f, theme::separator);
    ui.text(panel.x + 18.0f, panel.y + 5.0f, text("tab.console"), theme::text);

    const std::vector<Log::Entry>& lines = Log::entries();
    int warnings = 0, errors = 0, infos = 0;
    for (const Log::Entry& entry : lines) {
        if (entry.level == Log::Level::Error) ++errors;
        else if (entry.level == Log::Level::Warn) ++warnings;
        else ++infos;
    }

    const LevelFilter filters[3] = {
        {&consoleWarn_, warnings, text("console.warnings"), theme::warning},
        {&consoleError_, errors, text("console.errors"), theme::error},
        {&consoleInfo_, infos, text("console.info"), theme::textDim},
    };

    // Right to left, so the leftmost filter ends up next to the title.
    float x = panel.x + panel.width - 8.0f;
    for (int i = 2; i >= 0; --i) {
        const LevelFilter& filter = filters[i];
        char label[48];
        std::snprintf(label, sizeof(label), "%s %d", filter.label, filter.count);
        const float width = ui.textWidth(label) + 24.0f;
        const Rect hit{x - width, panel.y + 2.0f, width, kHeaderHeight - 4.0f};
        if (ui_.button(ui, kConsoleFilter + i, hit.x, hit.y, hit.width, hit.height, "",
                       *filter.flag))
            *filter.flag = !*filter.flag;

        // An outlined box that fills in when the level is included: drawn by hand
        // because a glyph for a tick cannot be relied on and a shape cannot go
        // missing from the atlas.
        const Color box = *filter.flag ? filter.color : theme::textDim;
        const float boxY = hit.y + (hit.height - 10.0f) * 0.5f;
        const float bx = hit.x + 6.0f;
        if (*filter.flag) ui.fillRect(bx, boxY, 10.0f, 10.0f, box);
        ui.fillRect(bx, boxY, 10.0f, 1.0f, box);
        ui.fillRect(bx, boxY + 9.0f, 10.0f, 1.0f, box);
        ui.fillRect(bx, boxY, 1.0f, 10.0f, box);
        ui.fillRect(bx + 9.0f, boxY, 1.0f, 10.0f, box);
        ui.text(bx + 15.0f, hit.y + (hit.height - theme::textLineHeight) * 0.5f, label,
                *filter.flag ? theme::text : theme::textDim);
        x -= width + 4.0f;
    }

    // Body: newest at the bottom, filtered, as many lines as fit.
    const float bodyTop = panel.y + kHeaderHeight;
    const int capacity = std::max(1, (int)((panel.height - kHeaderHeight - 6.0f) / kLineHeight));
    int drawn = 0;
    for (int i = (int)lines.size() - 1; i >= 0 && drawn < capacity; --i) {
        const Log::Entry& entry = lines[(size_t)i];
        const bool wanted = entry.level == Log::Level::Error  ? consoleError_
                            : entry.level == Log::Level::Warn ? consoleWarn_
                                                             : consoleInfo_;
        if (!wanted) continue;
        Color color{0.62f, 0.65f, 0.70f};
        if (entry.level == Log::Level::Error) color = theme::error;
        else if (entry.level == Log::Level::Warn) color = theme::warning;
        ui.textFit(panel.x + 8.0f, bodyTop + 4.0f + (float)drawn * kLineHeight, panel.width - 16.0f, entry.text, color);
        ++drawn;
    }
    if (drawn == 0)
        ui.text(panel.x + 8.0f, bodyTop + 4.0f, text("bar.consoleEmpty"), theme::textDim);
}

} // namespace s3d
