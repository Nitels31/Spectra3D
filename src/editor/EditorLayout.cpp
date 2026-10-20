#include "editor/Editor.hpp"

#include "core/Log.hpp"
#include "core/Math.hpp"
#include "core/Project.hpp"
#include "core/Str.hpp"
#include "platform/Filesystem.hpp"
#include "ui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace s3d {
namespace {

// Schema version this build writes. A file with a higher number is still read -
// every key here is optional - but it is reported, because it means another build
// laid the workspace out.
// 2: the favorites and the sounds moved into the bottom corners. A file
// written before that has them floating and closed, and the two keep the
// places they have now.
// 3: the three panels of the bottom row got heights of their own. A file
// written before that has one height for the whole row, and the three share
// it - which is what made the corners impossible to size on their own.
constexpr int kLayoutVersion = 3;

// `key=value` lines, `#` comments, blank lines skipped: the same shape as the
// manifest, so one reader's rules apply to every text file the engine writes.
template <typename Fn>
void forEachEntry(const std::string& text, Fn fn) {
    size_t at = 0;
    while (at <= text.size()) {
        size_t end = text.find('\n', at);
        const bool last = end == std::string::npos;
        if (last) end = text.size();

        const std::string line = str::trim(text.substr(at, end - at));
        at = end + 1;
        if (!line.empty() && line[0] != '#') {
            const size_t equals = line.find('=');
            if (equals != std::string::npos)
                fn(str::trim(line.substr(0, equals)), str::trim(line.substr(equals + 1)));
        }
        if (last) break;
    }
}

bool parseFloat(const std::string& text, float& out) {
    if (text.empty()) return false;
    char* stop = nullptr;
    const double value = std::strtod(text.c_str(), &stop);
    if (stop == text.c_str() || !std::isfinite(value)) return false;
    out = (float)value;
    return true;
}

bool parseInt(const std::string& text, int& out) {
    if (text.empty()) return false;
    char* stop = nullptr;
    const long value = std::strtol(text.c_str(), &stop, 10);
    if (stop == text.c_str()) return false;
    if (value < -2147483647L || value > 2147483647L) return false;
    out = (int)value;
    return true;
}

std::string formatFloat(float value) {
    char buffer[32];
    // One decimal is a pixel, and a workspace file nobody can read is one nobody
    // edits by hand when it needs fixing.
    std::snprintf(buffer, sizeof(buffer), "%.1f", (double)value);
    return buffer;
}

// The panel names in the file. Written out rather than numbered: a file that says
// `panel=Console,Bottom,1,...` can be understood and corrected by whoever finds
// it, and a renumbered one cannot.
const char* panelKey(int index) {
    switch (index) {
    case (int)PanelId::Hierarchy: return "Hierarchy";
    case (int)PanelId::Inspector: return "Inspector";
    case (int)PanelId::Console: return "Console";
    case (int)PanelId::Favorites: return "Favorites";
    case (int)PanelId::Sounds: return "Sounds";
    default: return "";
    }
}

int panelFromKey(const std::string& key) {
    for (int i = 0; i < kPanelCount; ++i)
        if (key == panelKey(i)) return i;
    return -1;
}

const char* zoneName(PanelZone zone) {
    switch (zone) {
    case PanelZone::Left: return "Left";
    case PanelZone::Right: return "Right";
    case PanelZone::Bottom: return "Bottom";
    case PanelZone::BottomLeft: return "BottomLeft";
    case PanelZone::BottomRight: return "BottomRight";
    case PanelZone::Float: return "Float";
    }
    return "Float";
}

PanelZone zoneFromName(const std::string& name) {
    if (name == "Left") return PanelZone::Left;
    if (name == "Right") return PanelZone::Right;
    if (name == "Bottom") return PanelZone::Bottom;
    if (name == "BottomLeft") return PanelZone::BottomLeft;
    if (name == "BottomRight") return PanelZone::BottomRight;
    return PanelZone::Float;
}

// `key,v0,v1,...` split on commas, trimmed. Empty fields are kept, because a
// missing number has to be told apart from a zero.
std::vector<std::string> splitCommas(const std::string& value) {
    std::vector<std::string> parts;
    size_t at = 0;
    while (true) {
        const size_t comma = value.find(',', at);
        if (comma == std::string::npos) {
            parts.push_back(str::trim(value.substr(at)));
            break;
        }
        parts.push_back(str::trim(value.substr(at, comma - at)));
        at = comma + 1;
    }
    return parts;
}

} // namespace

    // The saved workspace lives in the project it describes, next to the scene: two

// projects are two workspaces, and moving the folder moves the layout with it.
// The editor can also run with no project at all, and then it falls back to the
// directory of the binary - the same place settings.ini goes.
std::string Editor::layoutPath() const {
    const std::string dir = projectDir();
    if (!dir.empty() && dir != ".") return str::join(dir, kEditorLayoutFile);
    return str::join(fs::executableDir(), kEditorLayoutFile);
}

void Editor::loadLayout() {
    const std::string path = layoutPath();
    std::string text;
    if (!fs::readFile(path, text)) return;  // no file yet: the defaults are the layout

    // Read into copies first. A half-parsed file must not be able to leave half a
    // workspace on screen, and the values below are all validated before any of
    // them replaces what the editor is already using.
    float left = leftWidth_, right = rightWidth_, bottom = bottomHeight_;
    float bottomLeft = bottomLeftHeight_, bottomRight = bottomRightHeight_;
    // What the file said, before the sizes were clamped. A file that says
    // something the editor cannot use is rewritten with what it does use, and
    // these are the numbers that tell the two apart.
    float saidLeft = left, saidRight = right, saidBottom = bottom;
    float saidBottomLeft = bottomLeft, saidBottomRight = bottomRight;
    bool favoritesOnly = favoritesOnly_;
    Panel loaded[kPanelCount];
    bool seen[kPanelCount] = {};
    int version = 0;

    forEachEntry(text, [&](const std::string& key, const std::string& value) {
        if (key == "version") {
            (void)parseInt(value, version);
        } else if (key == "leftWidth") {
            if (parseFloat(value, left)) {
                saidLeft = left;
                left = clampf(left, kDockMinWidth, kDockMaxWidth);
            }
        } else if (key == "rightWidth") {
            if (parseFloat(value, right)) {
                saidRight = right;
                right = clampf(right, kDockMinWidth, kDockMaxWidth);
            }
        } else if (key == "bottomHeight") {
            // Bounded by the window as well as by a floor: the same ceiling a
            // drag stops at, so a hand-edited number cannot swallow the scene.
            if (parseFloat(value, bottom)) {
                saidBottom = bottom;
                bottom = clampf(bottom, kConsoleMinHeight,
                                std::fmax(kConsoleMinHeight, (float)fbHeight_ * 0.7f));
            }
        } else if (key == "bottomLeftHeight") {
            if (parseFloat(value, bottomLeft)) {
                saidBottomLeft = bottomLeft;
                bottomLeft = clampf(bottomLeft, kConsoleMinHeight,
                                    std::fmax(kConsoleMinHeight, (float)fbHeight_ * 0.7f));
            }
        } else if (key == "bottomRightHeight") {
            if (parseFloat(value, bottomRight)) {
                saidBottomRight = bottomRight;
                bottomRight = clampf(bottomRight, kConsoleMinHeight,
                                    std::fmax(kConsoleMinHeight, (float)fbHeight_ * 0.7f));
            }
        } else if (key == "favoritesOnly") {
            int flag = 0;
            if (parseInt(value, flag)) favoritesOnly = flag != 0;
        } else if (key == "panel") {
            const std::vector<std::string> parts = splitCommas(value);
            if (parts.size() < 7) return;
            const int index = panelFromKey(parts[0]);
            if (index < 0) return;

            Panel& panel = loaded[index];
            panel.zone = zoneFromName(parts[1]);
            int open = 1;
            (void)parseInt(parts[2], open);
            panel.open = open != 0;

            // A panel that has never floated has no rectangle of its own yet, and
            // the file says so with zeroes. Those keep the size the editor was
            // built with, so undocking one later opens a window rather than a
            // 160x88 speck in the corner.
            float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
            (void)parseFloat(parts[3], x);
            (void)parseFloat(parts[4], y);
            const bool hasSize = parseFloat(parts[5], width) && parseFloat(parts[6], height) &&
                                 width >= 1.0f && height >= 1.0f;
            if (hasSize) {
                panel.floatRect.x = x;
                panel.floatRect.y = y;
                panel.floatRect.width =
                    clampf(width, std::fmin(kPanelMinWidth, (float)fbWidth_),
                           std::fmax(kPanelMinWidth, (float)fbWidth_));
                panel.floatRect.height =
                    clampf(height, std::fmin(kPanelMinHeight, (float)fbHeight_),
                           std::fmax(kPanelMinHeight, (float)fbHeight_));
                panel.floatRect.x = clampf(
                    panel.floatRect.x, 0.0f,
                    std::fmax(0.0f, (float)fbWidth_ - panel.floatRect.width));
                panel.floatRect.y = clampf(
                    panel.floatRect.y, (float)theme::menuBarHeight,
                    std::fmax(0.0f, (float)fbHeight_ - panel.floatRect.height));
            }
            seen[index] = true;
        }
        // Unknown keys are ignored: a newer build may add some.
    });

    if (version > kLayoutVersion)
        Log::info("Editor: layout '%s' was written by a newer build (v%d)", path.c_str(), version);
    Log::info("Editor: layout '%s' loaded (v%d): left %.0f, right %.0f, bottom %.0f/%.0f/%.0f",
              path.c_str(), version, left, right, bottom, bottomLeft, bottomRight);

    // One panel per zone. The file cannot produce two (a swap always keeps the
    // count), but a hand-edited one can, and two panels in one zone would have
    // them drawn on top of each other - so the later one floats instead. Float is
    // the last zone, so indexing by zone covers exactly the docked ones.
    //
    // A file written before the panels had corners of their own has them
    // floating and closed, because there was nowhere else for them to be. The
    // file is right about everything else and wrong about those two, so they
    // keep the places they have now - and the file says which version wrote it,
    // which is what tells the two apart.
    const bool oldLayout = version < 2;
    if (oldLayout)
        Log::info("Editor: layout '%s' is from before the panels had corners; "
                  "the favorites and the sounds keep their new places",
                  path.c_str());

    // A file from before the bottom row had three heights of its own has one
    // height for the whole row, and the three share it. The corners keep the
    // height they have now, and the console keeps the one the file says.
    if (version < 3) {
        bottomLeft = bottomLeftHeight_;
        bottomRight = bottomRightHeight_;
    }

    bool edgeTaken[(int)PanelZone::Float] = {};
    for (int i = 0; i < kPanelCount; ++i) {
        if (!seen[i]) continue;
        Panel& target = panels_[i];
        const bool moved = i == (int)PanelId::Favorites || i == (int)PanelId::Sounds;
        if (oldLayout && moved) {
            // The place the panel has now, and the rectangle it had when it
            // was floating - so that dragging it out lands where it used to.
            target.floatRect = loaded[i].floatRect;
            continue;
        }
        const PanelZone zone = loaded[i].zone;
        if (zone != PanelZone::Float && loaded[i].open && edgeTaken[(int)zone]) {
            Log::warn("Editor: two panels want the %s edge, '%s' floats", zoneName(zone),
                      panelKey(i));
            target.zone = PanelZone::Float;
        } else {
            target.zone = zone;
            // Only an open panel occupies its edge: a closed one still remembers
            // where it was, and must not take the edge away from the panel that
            // is actually on it.
            if (zone != PanelZone::Float && loaded[i].open) edgeTaken[(int)zone] = true;
        }
        target.open = loaded[i].open;
        target.floatRect = loaded[i].floatRect;
    }
    leftWidth_ = left;
    rightWidth_ = right;
    bottomHeight_ = bottom;
    bottomLeftHeight_ = bottomLeft;
    bottomRightHeight_ = bottomRight;
    favoritesOnly_ = favoritesOnly;

    // A file that says something the editor cannot use is rewritten with what it
    // does use. The sizes are clamped - a hand-edited width of 999 is not a width
    // there is - and a file that keeps the impossible number is a file that will
    // be read wrong again, and that says one thing and does another.
    if (saidLeft != left || saidRight != right || saidBottom != bottom ||
        saidBottomLeft != bottomLeft || saidBottomRight != bottomRight) {
        Log::info("Editor: layout '%s' has sizes the editor cannot use, writing back what it "
                  "does use",
                  path.c_str());
        markLayoutDirty();
    }
}

void Editor::saveLayout() {
    const std::string path = layoutPath();

    std::string text;
    text.reserve(384);
    text += "# Spectra3D editor layout\n";
    text += "version=";
    text += std::to_string(kLayoutVersion);
    text += "\n";
    text += "leftWidth=" + formatFloat(leftWidth_) + "\n";
    text += "rightWidth=" + formatFloat(rightWidth_) + "\n";
    text += "bottomHeight=" + formatFloat(bottomHeight_) + "\n";
    text += "bottomLeftHeight=" + formatFloat(bottomLeftHeight_) + "\n";
    text += "bottomRightHeight=" + formatFloat(bottomRightHeight_) + "\n";
    text += "favoritesOnly=";
    text += favoritesOnly_ ? "1" : "0";
    text += "\n";
    // Every panel, every time, in a fixed order: the file is meant to be readable
    // and hand-editable, so it must not depend on what the user happened to touch.
    for (int i = 0; i < kPanelCount; ++i) {
        const Panel& panel = panels_[i];
        text += "panel=";
        text += panelKey(i);
        text += ',';
        text += zoneName(panel.zone);
        text += ',';
        text += panel.open ? "1" : "0";
        text += ',';
        text += formatFloat(panel.floatRect.x);
        text += ',';
        text += formatFloat(panel.floatRect.y);
        text += ',';
        text += formatFloat(panel.floatRect.width);
        text += ',';
        text += formatFloat(panel.floatRect.height);
        text += '\n';
    }

    if (!fs::writeFile(path, text)) Log::warn("Editor: cannot write the layout '%s'", path.c_str());
    else Log::info("Editor: layout '%s' saved", path.c_str());
}

} // namespace s3d