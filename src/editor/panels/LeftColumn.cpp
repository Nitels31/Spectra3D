#include "../Editor.hpp"

#include "../../core/Str.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cstring>

namespace s3d {
namespace {

constexpr float kHeaderHeight = 24.0f;
constexpr float kRowStep = theme::rowHeight;

// Case-insensitive substring test, both sides already prepared: `needle` is folded
// once by the caller, `haystack` is folded here as it is walked. An empty needle
// never matches, so an empty filter takes its own path instead of this one.
bool containsFolded(const char* haystack, const char* needle) {
    if (!haystack || !needle || !*needle) return false;
    for (const char* p = haystack; *p; ++p) {
        size_t matched = 0;
        while (needle[matched] && str::lowerAscii(p[matched]) == needle[matched]) ++matched;
        if (!needle[matched]) return true;
    }
    return false;
}


// Per-shape body tint so the hierarchy reads as a scene rather than a list.
Color rowTint(int index) {
    return Color{0.55f + 0.05f * (index & 3), 0.55f + 0.04f * ((index >> 1) & 3),
                 0.55f + 0.06f * ((index >> 2) & 3)};
}

// Header strip shared by both docks in this column.
void dockHeader(UiRenderer& ui, UiContext& ctx, const Rect& dock, const char* title,
                int fbWidth) {
    ui.fillRect(dock.x, dock.y, dock.width, kHeaderHeight, theme::panelDark);
    ui.text(dock.x + 8.0f, dock.y + 5.0f, title, theme::text);
    ui.fillRect(dock.x, dock.y + kHeaderHeight - 1.0f, dock.width, 1.0f, theme::separator);
    (void)ctx;
    (void)fbWidth;
}

} // namespace

// The kind of a body as its row shows it: a mesh name, or the primitive's name.
// Borrowed, not copied - the caller formats it into a stack buffer.
const char* Editor::bodyKind(const PhysicsWorld& world, int index) const {
    const RigidBody& body = world.bodies[(size_t)index];
    return body.hasMesh() ? meshes_.nameOf(body.mesh).c_str() : shapeName(body.shape);
}

void Editor::rebuildFilter(const PhysicsWorld& world, const char* query) {
    filterRows_.clear();
    const int count = (int)world.bodies.size();
    const size_t queryLength = query ? std::char_traits<char>::length(query) : 0;
    if (queryLength == 0) {
        // No filter: every body, filled straight in without building a label.
        filterRows_.resize((size_t)count);
        for (int index = 0; index < count; ++index) filterRows_[(size_t)index] = index;
        return;
    }

    char needle[64];
    const size_t folded = std::min(queryLength, sizeof(needle) - 1);
    for (size_t i = 0; i < folded; ++i) needle[i] = str::lowerAscii(query[i]);
    needle[folded] = '\0';

    // The label a row would show, formatted once per body into one reused buffer
    // and matched in place. No allocation, one pass, and the filter still tests
    // exactly the string on screen.
    char label[64];
    for (int index = 0; index < count; ++index) {
        formatIndexed(label, sizeof(label), bodyKind(world, index), ' ', index);
        if (containsFolded(label, needle)) filterRows_.push_back(index);
    }
}

void Editor::drawLeftColumn(UiRenderer& ui, PhysicsWorld& world, Camera& camera,
                            Window& window, int fbWidth) {
    // One panel, full height. The sketch has the hierarchy and nothing else in
    // this column; the model list that used to sit under it is gone, because a
    // permanent list of files that never changes is a panel that costs a redraw on
    // every frame to say nothing - the same files are one menu away, under Create.
    drawHierarchyDock(ui, world, camera, window, fbWidth);
}

void Editor::drawHierarchyDock(UiRenderer& ui, PhysicsWorld& world, Camera& camera,
                               Window& window, int fbWidth) {
    const Rect dock = layout_.leftDock;
    ui.fillRect(dock.x, dock.y, dock.width, dock.height, theme::panel);
    dockHeader(ui, ui_, dock, text("tab.hierarchy"), fbWidth);


    // --- search ---
    const Rect search{dock.x + 6.0f, dock.y + kHeaderHeight + 4.0f, dock.width - 12.0f,
                      theme::fieldHeight};
    ui_.pumpTextField(window, hierarchyFilter_, ui_.hovered(search));
    ui.fillRect(search.x, search.y, search.width, search.height,
                hierarchyFilter_.editing ? theme::accent.scaled(0.35f) : theme::field);

    // Magnifier, small and clear of the text: drawn at text size it reads as a
    // letter and looks like the start of the word.
    const float iconX = search.x + 9.0f;
    const float iconY = search.y + search.height * 0.45f;
    ui.fillCircle(iconX, iconY, 3.5f, theme::textDim);
    ui.fillCircle(iconX, iconY, 2.2f,
                  hierarchyFilter_.editing ? theme::accent.scaled(0.35f) : theme::field);
    ui.fillRect(iconX + 2.5f, iconY + 2.5f, 3.5f, 1.5f, theme::textDim);
    const char* searchText =
        hierarchyFilter_.empty() ? text("hierarchy.search") : hierarchyFilter_.text();
    ui.text(search.x + 18.0f, search.y + (search.height - theme::textLineHeight) * 0.5f,
            searchText, hierarchyFilter_.empty() ? theme::textDim : theme::text);
    if (hierarchyFilter_.editing)
        ui.fillRect(search.x + 18.0f + ui.textWidth(hierarchyFilter_.text()) + 1.0f, search.y + 3.0f,
                    1.5f, search.height - 6.0f, theme::accent);

    // --- tree ---
    const float rowsTop = search.y + search.height + 4.0f;
    const float rowsBottom = dock.y + dock.height;
    const int capacity = std::max(1, (int)((rowsBottom - rowsTop) / kRowStep));

    // The filtered index list is built ONCE per frame into a member vector that
    // keeps its capacity between frames.
    //
    // The obvious version - count the matches, then walk the bodies again for
    // every visible row - is quadratic and builds a std::string per body per
    // comparison. With 64 bodies and 20 visible rows that is over a thousand
    // allocations every frame, and the hierarchy is the one panel that is on
    // screen while nothing else happens.
    rebuildFilter(world, hierarchyFilter_.text());

    const int totalRows = (int)filterRows_.size() + 1;  // the scene root plus what passed
    const int maxScroll = std::max(0, totalRows - capacity);
    hierarchyScroll_ = clampf(hierarchyScroll_ - (float)ui_.scrollDelta() * 2.0f, 0.0f,
                              (float)maxScroll);

    for (int row = 0; row < capacity && row + (int)hierarchyScroll_ < totalRows; ++row) {
        const int drawn = row + (int)hierarchyScroll_;
        const float y = rowsTop + (float)row * kRowStep;
        const Rect hit{dock.x + 4.0f, y, dock.width - 8.0f, kRowStep};
        const bool hovered = ui_.hovered(hit);

        if (drawn == 0) {
            if (hovered) ui.fillRect(hit.x, hit.y, hit.width, hit.height, theme::header);
            ui.fillRect(dock.x + 10.0f, y + 5.0f, 10.0f, 10.0f, theme::accent);
            ui.text(dock.x + 28.0f, y + 2.0f, text("hier.sceneRoot"), theme::text);
            if (hovered && ui_.pointerPressed()) {
                selectedBodies_.clear();
                activeBody_ = -1;
            }
            continue;
        }

        const int index = filterRows_[(size_t)(drawn - 1)];
        const RigidBody& body = world.bodies[(size_t)index];
        const bool selected =
            index == activeBody_ ||
            std::find(selectedBodies_.begin(), selectedBodies_.end(), index) !=
                selectedBodies_.end();
        if (selected)
            ui.fillRect(hit.x, hit.y, hit.width, hit.height, theme::accentDim);
        else if (hovered)
            ui.fillRect(hit.x, hit.y, hit.width, hit.height, theme::header);

        const Color tint = rowTint(index);
        if (body.shape == Shape::Sphere)
            ui.fillCircle(dock.x + 31.0f, y + 10.0f, 5.0f, tint);
        else
            ui.fillRect(dock.x + 26.0f, y + 5.0f, 10.0f, 10.0f, tint);

        // Built into a stack buffer, not a std::string: this runs for every visible row
        // every frame, and the heap traffic showed up as a stuttering hierarchy.
        char label[64];
        formatIndexed(label, sizeof(label), bodyKind(world, index), ' ', index);
        ui.textFit(dock.x + 44.0f, y + 2.0f, dock.width - 74.0f, label, theme::text);
        // Right-hand flags, drawn over the reserved strip so the name can use the
        // rest without colliding with them.
        if (!body.visible) ui.text(dock.x + dock.width - 30.0f, y + 2.0f, "H", theme::textDim);
        if (body.material.isStatic)
            ui.text(dock.x + dock.width - 18.0f, y + 2.0f, "S", theme::warning);

        if (hovered) ui_.markHovered(kHierarchyRow + index);
        if (hovered && ui_.pointerPressed()) {
            if (ui_.ctrlHeld()) {
                const auto it =
                    std::find(selectedBodies_.begin(), selectedBodies_.end(), index);
                if (it != selectedBodies_.end()) selectedBodies_.erase(it);
                else selectedBodies_.push_back(index);
                activeBody_ = index;
            } else {
                selectedBodies_.assign(1, index);
                activeBody_ = index;
            }
        }
    }

    // "Nothing matches" instead of an empty box with no explanation. Below the
    // root row, which is always there - it is what a filter returns to.
    if (filterRows_.empty()) {
        ui.textFit(dock.x + 10.0f, rowsTop + kRowStep + 4.0f, dock.width - 20.0f,
                   text("hierarchy.noMatches"), theme::textDim);
    }
}

} // namespace s3d
