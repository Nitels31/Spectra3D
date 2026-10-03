#include "../Editor.hpp"

#include "../../core/Str.hpp"
#include "../../scene/BodyFactory.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>

namespace s3d {
namespace {

struct SpawnButton {
    Shape shape;
    const char* label;
};

const SpawnButton kSpawnButtons[7] = {
    {Shape::Box, "Box"},         {Shape::Sphere, "Sphere"},   {Shape::Cylinder, "Cylinder"},
    {Shape::Capsule, "Capsule"}, {Shape::Cone, "Cone"},       {Shape::Pyramid, "Pyramid"},
    {Shape::Prism, "Prism"},
};

// Per-shape body tint so the hierarchy reads as a scene, not a list.
Color rowTint(int index) {
    return Color{0.55f + 0.05f * (index & 3), 0.55f + 0.04f * ((index >> 1) & 3),
                 0.55f + 0.06f * ((index >> 2) & 3)};
}

} // namespace

void Editor::drawSceneDock(UiRenderer& ui, PhysicsWorld& world, Camera& camera,
                           int fbHeight) {
    const Rect dock = layout_.leftDock;
    ui.fillRect(dock.x, dock.y, dock.width, dock.height, theme::panel);

    const float halfWidth = dock.width * 0.5f;
    if (ui_.tab(ui, dock.x, dock.y, halfWidth, text("tab.hierarchy"), sceneTab_ == 0))
        sceneTab_ = 0;
    if (ui_.tab(ui, dock.x + halfWidth, dock.y, halfWidth, text("tab.assets"), sceneTab_ == 1))
        sceneTab_ = 1;
    ui.fillRect(dock.x, dock.y + 24.0f, dock.width, 1.0f, theme::separator);

    if (sceneTab_ == 1) {
        drawAssetDock(ui, world, camera, dock);
        return;
    }

    drawHierarchy(ui, world, camera, dock);
}

void Editor::drawHierarchy(UiRenderer& ui, PhysicsWorld& world, Camera& camera,
                           const Rect& dock) {
    const float x = dock.x;
    const float width = dock.width;
    float rowY = dock.y + 30.0f;
    const float treeBottom = dock.y + dock.height - 260.0f;
    const int bodyCount = (int)world.bodies.size();
    const int totalRows = bodyCount + 1; // scene root + bodies

    int visibleRows = (int)((treeBottom - rowY) / theme::rowHeight);
    visibleRows = std::max(1, visibleRows);
    const int maxScroll = std::max(0, totalRows - visibleRows);
    hierarchyScroll_ =
        clampf(hierarchyScroll_ - (float)ui_.scrollDelta() * 2.0f, 0.0f, (float)maxScroll);
    const int topRow = (int)hierarchyScroll_;

    for (int row = topRow; row < totalRows && row < topRow + visibleRows; ++row) {
        const float y = rowY + (float)(row - topRow) * theme::rowHeight;
        const Rect rowRect{x + 4.0f, y, width - 8.0f, theme::rowHeight};
        const bool hovered = ui_.hovered(rowRect);

        if (row == 0) {
            if (hovered) ui.fillRect(rowRect.x, rowRect.y, rowRect.width, rowRect.height,
                                     theme::header);
            ui.fillRect(x + 10.0f, y + 5.0f, 10.0f, 10.0f, theme::accent);
            ui.text(x + 28.0f, y + 2.0f, text("hier.sceneRoot"), theme::text);
            if (ui_.pointerPressed() && hovered) {
                selectedBodies_.clear();
                activeBody_ = -1;
            }
            continue;
        }

        const int index = row - 1;
        const RigidBody& body = world.bodies[index];
        const bool isSelected =
            index == activeBody_ ||
            std::find(selectedBodies_.begin(), selectedBodies_.end(), index) !=
                selectedBodies_.end();
        if (isSelected) {
            ui.fillRect(rowRect.x, rowRect.y, rowRect.width, rowRect.height, theme::accentDim);
        } else if (hovered) {
            ui.fillRect(rowRect.x, rowRect.y, rowRect.width, rowRect.height, theme::header);
        }

        const char* kind = body.hasMesh() ? meshes_.nameOf(body.mesh).c_str()
                                          : shapeName(body.shape);
        char label[64];
        std::snprintf(label, sizeof(label), "%s %02d", kind, index);

        const Color tint = rowTint(index);
        if (body.shape == Shape::Sphere)
            ui.fillCircle(x + 31.0f, y + 10.0f, 5.0f, tint);
        else
            ui.fillRect(x + 26.0f, y + 5.0f, 10.0f, 10.0f, tint);

        ui.text(x + 44.0f, y + 2.0f, label, theme::text);
        if (!body.visible) ui.text(x + width - 30.0f, y + 2.0f, "H", theme::textDim);
        if (body.material.isStatic)
            ui.text(x + width - 18.0f, y + 2.0f, "S", theme::warning);

        if (hovered) ui_.markHovered(kHierarchyRow + index);
        if (ui_.pointerPressed() && hovered) {
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

    if (playMode_) return;

    // Creation palette, hidden while simulating.
    float y = dock.y + dock.height - 258.0f;
    ui_.sectionHeader(ui, x, y, width, text("sec.create"));
    y += 26.0f;

    constexpr float kGap = 6.0f, kHeight = 26.0f;
    const float bx = x + 6.0f;
    const float available = width - 12.0f;
    const float column3 = (available - kGap * 2.0f) / 3.0f;
    const float column2 = (available - kGap) * 0.5f;

    for (int i = 0; i < 7; ++i) {
        const int row = i / 3;
        const int column = i % 3;
        const bool lastRow = row == 2;
        const float cellWidth = lastRow ? column2 : column3;
        const float cellX = lastRow ? bx : bx + (column3 + kGap) * (float)column;
        const float cellY = y + (float)row * (kHeight + kGap);

        if (ui_.button(ui, kCreateButton + i, cellX, cellY, cellWidth, kHeight, kSpawnButtons[i].label))
            BodyFactory::spawnInFrontOf(world, kSpawnButtons[i].shape, camera, 1.0f);

        if (lastRow)
            ui.text(cellX + cellWidth + kGap + 4.0f, cellY + 6.0f, "(5+ sides)", theme::textDim);
    }
}

void Editor::drawAssetDock(UiRenderer& ui, PhysicsWorld& world, Camera& camera,
                           const Rect& dock) {
    if (!assets_.scanned()) assets_.refresh();

    const float x = dock.x;
    float y = dock.y + 38.0f;
    ui.text(x + 10.0f, y, text("assets.import"), theme::text);

    if (ui_.button(ui, kAssetDock, x + dock.width - 80.0f, y, 70.0f, 20.0f, text("btn.refresh")))
        assets_.refresh();

    y += 22.0f;
    const std::vector<std::string>& files = assets_.files();
    for (size_t i = 0; i < files.size() && i < 10; ++i) {
        if (ui_.button(ui, kImportRow + i, x + 6.0f, y, dock.width - 12.0f, 24.0f,
                       str::fileName(files[i]).c_str()))
            assets_.import(files[i], world, camera, meshes_, activeBody_);
        y += 28.0f;
    }
    if (files.empty()) ui.text(x + 10.0f, y, text("assets.noModels"), theme::textDim);
}

} // namespace s3d