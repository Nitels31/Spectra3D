#include "../Editor.hpp"

#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cstdio>

namespace s3d {
namespace {

constexpr float kHeaderHeight = 24.0f;
constexpr float kRowStep = theme::rowHeight;
// Same column as in the hierarchy, so a name reads the same in both panels.
constexpr float kStarSize = 16.0f;
constexpr float kStarX = 6.0f;

// Per-shape tint, matching the hierarchy so one body looks the same in both
// lists. Duplicated on purpose rather than shared: the hierarchy's copy is in an
// anonymous namespace of its own file, and a shared symbol would be a wider
// contract than two lines of colour arithmetic.
Color rowTint(int index) {
    return Color{0.55f + 0.05f * (index & 3), 0.55f + 0.04f * ((index >> 1) & 3),
                 0.55f + 0.06f * ((index >> 2) & 3)};
}

} // namespace

// The Favorites panel: the bodies a star in the hierarchy set aside, and nothing
// else. It is a panel like the other three - dragged by its title bar, resized by
// its edges, closed with the X, back from the View menu - because a list of
// objects you care about is worth as much as the tree it came from.
void Editor::drawFavoritesDock(UiRenderer& ui, PhysicsWorld& world, const Rect& dock) {
    if (dock.width <= 0.0f || dock.height <= 0.0f) return;

    const char* title = text("tab.favorites");
    ui.fillRect(dock.x, dock.y, dock.width, kHeaderHeight, theme::panelDark);
    ui.fillRect(dock.x, dock.y + kHeaderHeight - 1.0f, dock.width, 1.0f, theme::separator);
    ui.text(dock.x + 18.0f, dock.y + 5.0f, title, theme::text);

    // The starred bodies, collected once per frame into a member list that keeps
    // its capacity: the panel redraws on every frame and the body count is not
    // bounded, so walking them twice per visible row is the alternative nobody
    // wants.
    favoriteRows_.clear();
    for (int index = 0; index < (int)world.bodies.size(); ++index)
        if (world.bodies[(size_t)index].favorite) favoriteRows_.push_back(index);

    // The count, next to the title rather than in place of it: "how many" is
    // answered by reading the list, and the list can be empty on purpose.
    char count[16];
    std::snprintf(count, sizeof(count), "%d", (int)favoriteRows_.size());
    ui.text(dock.x + 26.0f + ui.textWidth(title), dock.y + 5.0f, count, theme::textDim);

    if (favoriteRows_.empty()) {
        // Nothing here yet is a normal state, and it says what to do about it
        // rather than sitting as an empty box.
        const char* empty = text("favorites.empty");
        const char* hint = text("favorites.hint");
        const float y = dock.y + kHeaderHeight + 12.0f;
        ui.textFit(dock.x + 10.0f, y, dock.width - 20.0f, empty, theme::textDim);
        ui.textFit(dock.x + 10.0f, y + theme::textLineHeight + 6.0f, dock.width - 20.0f, hint,
                   theme::textDim.scaled(0.85f));
        favoritesScroll_ = 0.0f;
        return;
    }

    const float rowsTop = dock.y + kHeaderHeight + 4.0f;
    const int capacity = std::max(1, (int)((dock.y + dock.height - rowsTop) / kRowStep));
    const int maxScroll = std::max(0, (int)favoriteRows_.size() - capacity);
    // The wheel belongs to the panel under the pointer, the same rule the tree and
    // the inspector follow.
    favoritesScroll_ = clampf(favoritesScroll_, 0.0f, (float)maxScroll);
    if (ui_.hovered(dock) && ui_.scrollDelta() != 0.0)
        favoritesScroll_ = clampf(favoritesScroll_ - (float)ui_.scrollDelta() * 2.0f, 0.0f,
                                  (float)maxScroll);

    for (int row = 0; row < capacity; ++row) {
        const int slot = (int)favoritesScroll_ + row;
        if (slot >= (int)favoriteRows_.size()) break;

        // The scene index of this body, which is what selection, the star and the
        // name all speak. Everything else in the row is decoration around it.
        const int index = favoriteRows_[(size_t)slot];
        const RigidBody& body = world.bodies[(size_t)index];
        const float y = rowsTop + (float)row * kRowStep;
        const Rect hit{dock.x + 4.0f, y, dock.width - 8.0f, kRowStep};
        const bool hovered = ui_.hovered(hit);

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

        char label[64];
        formatIndexed(label, sizeof(label), bodyKind(world, index), ' ', index);
        ui.textFit(dock.x + 44.0f, y + 2.0f, dock.width - 74.0f, label, theme::text);
        if (!body.visible) ui.text(dock.x + dock.width - 30.0f, y + 2.0f, "H", theme::textDim);
        if (body.material.isStatic)
            ui.text(dock.x + dock.width - 18.0f, y + 2.0f, "S", theme::warning);

        // The star that un-favorites. It is the same gesture as in the hierarchy, in the
        // same column, so the two panels read as one list seen twice - and every
        // row here is starred by definition, so it is always drawn lit rather
        // than waiting for the pointer to explain what the panel is.
        const Rect star{dock.x + kStarX - 4.0f, y, kStarSize, kRowStep};
        const bool starHovered = ui_.hovered(star);
        drawStar(ui, star.x + kStarSize * 0.5f, y + kRowStep * 0.5f, 5.0f,
                 starHovered ? theme::warning.scaled(1.25f) : theme::warning);
        if (starHovered && ui_.pointerPressed()) {
            world.bodies[(size_t)index].favorite = false;
            markSceneDirty();
        }

        if (hovered) ui_.markHovered(kFavoriteRow + index);
        if (hovered && ui_.pointerPressed() && !starHovered) {
            if (ui_.ctrlHeld()) {
                const auto it = std::find(selectedBodies_.begin(), selectedBodies_.end(), index);
                if (it != selectedBodies_.end())
                    selectedBodies_.erase(it);
                else
                    selectedBodies_.push_back(index);
                activeBody_ = index;
            } else {
                selectedBodies_.assign(1, index);
                activeBody_ = index;
            }
        }
    }
}

} // namespace s3d