#include "../Editor.hpp"

#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace s3d {
namespace {

constexpr float kHeaderHeight = 24.0f;
constexpr float kRowStep = theme::rowHeight;

// Last path component of `path` as a borrowed pointer. Same text as
// str::fileName(), but this list redraws every frame and returning a std::string
// was a heap allocation per visible row per frame.
const char* fileNameTail(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path.c_str() : path.c_str() + slash + 1;
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

// The label a hierarchy row shows, and the two helpers the filter needs. All
// three build the label the same way, so the filter can never disagree with the
// row about what a body is called.
std::string Editor::bodyLabel(const PhysicsWorld& world, int index) const {
    const RigidBody& body = world.bodies[(size_t)index];
    const char* kind =
        body.hasMesh() ? meshes_.nameOf(body.mesh).c_str() : shapeName(body.shape);
    char label[64];
    formatIndexed(label, sizeof(label), kind, ' ', index);
    return label;
}

bool Editor::bodyMatchesFilter(const PhysicsWorld& world, int index, const char* query) const {
    if (!query || !*query) return true;
    return bodyLabel(world, index).find(query) != std::string::npos;
}

// Index of the `n`-th body that passes the filter. Walks from the start every
// time, which costs nothing for a panel showing a few dozen rows and avoids
// allocating an index list per frame.
int Editor::filteredBodyIndex(const PhysicsWorld& world, int n, const char* query) const {
    if (n < 0) return -1;
    int seen = 0;
    const int count = (int)world.bodies.size();
    for (int index = 0; index < count; ++index) {
        if (!bodyMatchesFilter(world, index, query)) continue;
        if (seen == n) return index;
        ++seen;
    }
    return -1;
}

void Editor::drawLeftColumn(UiRenderer& ui, PhysicsWorld& world, Camera& camera,
                            Window& window, int fbWidth) {
    drawHierarchyDock(ui, world, camera, window, layout_.hierarchyDock, fbWidth);
    drawAssetDock(ui, world, camera, layout_.assetDock, fbWidth);
}

void Editor::drawHierarchyDock(UiRenderer& ui, PhysicsWorld& world, Camera& camera,
                               Window& window, const Rect& dock, int fbWidth) {
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

    int matching = 0;
    for (int index = 0; index < (int)world.bodies.size(); ++index)
        if (bodyMatchesFilter(world, index, hierarchyFilter_.text())) ++matching;

    const int totalRows = matching + 1;  // the scene root plus what passed
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

        const int index = filteredBodyIndex(world, drawn - 1, hierarchyFilter_.text());
        if (index < 0) continue;
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

        // Shrunk into the slot left of the flag strip, never clipped: the name is
        // the only thing identifying a body.
        ui.textFit(dock.x + 44.0f, y + 2.0f, dock.width - 74.0f, bodyLabel(world, index).c_str(),
                   theme::text);
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
    if (matching == 0) {
        ui.textFit(dock.x + 10.0f, rowsTop + kRowStep + 4.0f, dock.width - 20.0f,
                   text("hierarchy.noMatches"), theme::textDim);
    }
}

void Editor::drawAssetDock(UiRenderer& ui, PhysicsWorld& world, Camera& camera,
                           const Rect& dock, int fbWidth) {
    ui.fillRect(dock.x, dock.y, dock.width, dock.height, theme::panel);
    dockHeader(ui, ui_, dock, text("tab.assets"), fbWidth);
    if (!assets_.scanned()) assets_.refresh();

    const float buttonW = ui.textWidth(text("btn.refresh")) + 18.0f;
    if (ui_.button(ui, kAssetDock, dock.x + dock.width - buttonW - 6.0f, dock.y + 2.0f, buttonW,
                   kHeaderHeight - 4.0f, text("btn.refresh")))
        assets_.refresh();

    float y = dock.y + kHeaderHeight + 6.0f;
    const std::vector<std::string>& files = assets_.files();
    for (size_t i = 0; i < files.size(); ++i) {
        const Rect row{dock.x + 6.0f, y, dock.width - 12.0f, 24.0f};
        if (y + 24.0f > dock.y + dock.height) break;
        if (ui_.button(ui, kImportRow + (int)i, row.x, row.y, row.width, row.height,
                       fileNameTail(files[i])))
            assets_.import(files[i], world, camera, meshes_, activeBody_);
        y += 26.0f;
    }
    if (files.empty())
        ui.text(dock.x + 10.0f, y, text("assets.noModels"), theme::textDim);
}

} // namespace s3d