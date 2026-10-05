#include "../Editor.hpp"

#include "../../core/Log.hpp"
#include "../../core/Project.hpp"
#include "../../core/Str.hpp"
#include "../../platform/Filesystem.hpp"
#include "../../scene/BodyFactory.hpp"
#include "../../ui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace s3d {
namespace {

// Metrics of the bar itself. Everything that hangs off it - the dropdowns, the
// layers panel, the search results - has its own metrics in TopBarOverlay.cpp.
constexpr float kBarButtonHeight = 20.0f;
constexpr float kBarGap = 4.0f;
constexpr float kSearchWidth = 172.0f;

// Shape names, in `Shape` order. A menu row carries the shape as its parameter,
// so these two have to stay in step - hence the count check.
const char* const kShapeLabels[] = {"Box",      "Sphere",   "Cylinder", "Capsule",
                                    "Cone",     "Pyramid",  "Prism"};
constexpr int kShapeCount = (int)(sizeof(kShapeLabels) / sizeof(kShapeLabels[0]));
static_assert(kShapeCount == 7, "one label per primitive in Shape, before Mesh");

const float kPerspectiveValues[3] = {60.0f, 70.0f, 90.0f};
const char* const kPerspectiveKeys[3] = {"vp.perspective60", "vp.perspective70",
                                         "vp.perspective90"};

// Case-insensitive substring test, `needle` folded once by the caller and
// `haystack` folded here as it walks. An empty needle never matches, so an empty
// search takes its own path instead of this one.
bool containsFolded(const char* haystack, const char* foldedNeedle) {
    if (!haystack || !foldedNeedle || !*foldedNeedle) return false;
    for (const char* p = haystack; *p; ++p) {
        size_t matched = 0;
        while (foldedNeedle[matched] && str::lowerAscii(p[matched]) == foldedNeedle[matched])
            ++matched;
        if (!foldedNeedle[matched]) return true;
    }
    return false;
}

// Glyphs are drawn from rects and circles, like the viewport tool icons: the
// atlas carries ASCII and Cyrillic and nothing else, so a button whose picture
// is U+25B6 or U+27F3 is a button that comes out blank.
enum class Glyph { Play, Pause, Stop, Reload, Search };

// A triangle rasterised as stacked rows - the only shape the 2D batcher cannot
// draw directly, and the play button wants one. `pointRight` mirrors it: false is
// a triangle with its apex up.
void triangle(UiRenderer& ui, float x, float y, float w, float h, bool pointRight,
              const Color& color) {
    constexpr int kRows = 9;
    const float rowHeight = h / (float)kRows;
    for (int i = 0; i < kRows; ++i) {
        const float t = ((float)i + 0.5f) / (float)kRows;
        const float width = w * (1.0f - std::fabs(t * 2.0f - 1.0f));
        const float left = pointRight ? x : x + (w - width) * 0.5f;
        ui.fillRect(left, y + (float)i * rowHeight, width, rowHeight + 0.5f, color);
    }
}

void drawGlyph(UiRenderer& ui, const Rect& rect, Glyph glyph, const Color& tint) {
    const float cx = rect.x + rect.width * 0.5f;
    const float cy = rect.y + rect.height * 0.5f;
    const float s = std::fmin(rect.width, rect.height);
    auto bar = [&](float x, float y, float w, float h) { ui.fillRect(x, y, w, h, tint); };

    switch (glyph) {
    case Glyph::Play:
        triangle(ui, cx - s * 0.30f, cy - s * 0.32f, s * 0.60f, s * 0.64f, true, tint);
        break;
    case Glyph::Pause: {
        const float w = s * 0.15f, h = s * 0.62f;
        bar(cx - w * 1.6f, cy - h * 0.5f, w, h);
        bar(cx + w * 0.6f, cy - h * 0.5f, w, h);
        break;
    }
    case Glyph::Stop: {
        const float side = s * 0.54f;
        bar(cx - side * 0.5f, cy - side * 0.5f, side, side);
        break;
    }
    case Glyph::Reload: {
        // A ring with a gap at the top and an arrow head in it. Drawn as dots
        // because a partial ring is not a shape the batcher has either.
        const float radius = s * 0.28f;
        constexpr int kSteps = 12;
        for (int i = 0; i < kSteps; ++i) {
            const float a = (float)i / (float)kSteps * 2.0f * kPi + kPi * 0.15f;
            ui.fillCircle(cx + std::cos(a) * radius, cy + std::sin(a) * radius, s * 0.055f,
                          tint);
        }
        triangle(ui, cx - s * 0.16f, cy - radius - s * 0.24f, s * 0.32f, s * 0.26f, false, tint);
        break;
    }
    case Glyph::Search: {
        // Circle plus a handle, both hollow in the middle so it reads as a lens
        // instead of a dot. The lens is punched back out in the field colour,
        // which is what the field behind it is.
        ui.fillCircle(cx, cy, s * 0.22f, tint);
        ui.fillCircle(cx, cy, s * 0.12f, theme::field);
        ui.fillRect(cx + s * 0.13f, cy + s * 0.13f, s * 0.22f, s * 0.09f, tint);
        break;
    }
    }
}

} // namespace

// Every row of every menu, rebuilt each frame.
//
// The toolbar is drawn on every frame that anything at all happens, so this is a
// fixed-size member array filled in place - no vector, no per-frame allocation for
// forty rows of text pointers. The model rows come from the asset browser, which
// is why Create is the one menu whose length changes.
void Editor::buildMenuRows() {
    if (!assets_.scanned()) assets_.refresh();

    int at = 0;
    auto add = [&](Action action, int param, const char* label, bool checked = false,
                   bool separator = false) {
        if (at >= kMaxMenuRows) return;
        MenuRow& row = menuRows_[at++];
        row.action = action;
        row.param = param;
        row.text = label;
        row.checked = checked;
        row.separator = separator;
    };
    auto open = [&](int menu) { menuFirst_[menu] = at; };
    auto close = [&](int menu) { menuFirst_[menu + 1] = at; };

    open(kMenuFile);
    add(Action::NewScene, 0, text("file.newScene"));
    add(Action::OpenScene, 0, text("file.openScene"));
    add(Action::Save, 0, text("file.save"));
    add(Action::SaveAs, 0, text("file.saveAs"));
    add(Action::NewProject, 0, text("file.newProject"));
    add(Action::OpenProject, 0, text("file.openProject"));
    add(Action::SaveProject, 0, text("file.saveProject"));
    add(Action::BuildSettings, 0, text("file.buildSettings"));
    add(Action::BuildRun, 0, text("file.buildRun"));
    add(Action::None, 0, nullptr, false, true);
    add(Action::Exit, 0, text("file.exit"));
    close(kMenuFile);

    open(kMenuEdit);
    add(Action::Duplicate, 0, text("edit.duplicate"));
    add(Action::Delete, 0, text("edit.delete"));
    add(Action::ToggleFavorite, 0, text("edit.favorite"));
    add(Action::None, 0, nullptr, false, true);
    add(Action::ClearSelection, 0, text("edit.clearSelection"));
    add(Action::Wake, 0, text("edit.wake"));
    close(kMenuEdit);

    open(kMenuView);
    add(Action::Grid, 0, text("view.grid"), showGrid_);
    add(Action::Vectors, 0, text("view.vectors"), showVectors_);
    add(Action::None, 0, nullptr, false, true);
    for (int i = 0; i < 3; ++i)
        add(Action::Fov, i, text(kPerspectiveKeys[i]), perspectiveIndex_ == i);
    add(Action::None, 0, nullptr, false, true);
    add(Action::FrameSelection, 0, text("view.frame"));
    add(Action::ResetCamera, 0, text("view.resetCamera"));
    add(Action::None, 0, nullptr, false, true);
    // Which panels are on screen. Closed with the X in a panel's header, reopened
    // from here - so "I do not need this panel" is one click and not a restart.
    for (int i = 0; i < kPanelCount; ++i)
        add(Action::TogglePanel, i, panelTitle((PanelId)i), panels_[i].open);
    close(kMenuView);

    open(kMenuCreate);
    for (int i = 0; i < kShapeCount; ++i)
        add(Action::Shape, i, kShapeLabels[i]);
    const std::vector<std::string>& models = assets_.files();
    add(Action::None, 0, nullptr, false, true);
    if (models.empty()) {
        add(Action::None, 0, text("assets.noModels"));
    } else {
        // Every model in the folder, every time. The list used to stop at ten and
        // print "+7" instead, which is indistinguishable from a folder that holds
        // ten files - and a Create menu that silently hides half the project's
        // models is worse than no menu. A menu that outgrows the window is bounded
        // by the window and scrolls; see drawTopBarOverlay.
        for (size_t i = 0; i < models.size(); ++i)
            add(Action::Model, (int)i, str::fileNameView(models[i]).data());
    }
    close(kMenuCreate);

    open(kMenuHelp);
    add(Action::Controls, 0, text("help.controls"));
    add(Action::About, 0, text("help.about"));
    close(kMenuHelp);

    menuFirst_[kMenuCount] = at;
}

void Editor::closeOverlays() {
    openMenu_ = -1;
    layersOpen_ = false;
    helpPage_ = 0;
    // Each menu starts at its top: a scroll left over from the last one opened
    // would drop a long Create menu halfway down its own list.
    menuScroll_ = 0.0f;
}

const char* Editor::menuCaption(int menu) const {
    static const char* const keys[kMenuCount] = {"menu.file", "menu.edit", "menu.view",
                                                 "menu.create", "menu.help"};
    return menu >= 0 && menu < kMenuCount ? text(keys[menu]) : "";
}

int Editor::findMenuRows(const char* query, MenuHit* out, int capacity) const {
    if (!query || !*query || capacity <= 0) return 0;

    // Folded once, into a buffer big enough for a menu caption plus slack: the
    // needle is compared against a few dozen captions and folding it per caption
    // is the same work done forty times.
    char needle[64];
    const size_t length = std::min(std::strlen(query), sizeof(needle) - 1);
    for (size_t i = 0; i < length; ++i) needle[i] = str::lowerAscii(query[i]);
    needle[length] = '\0';

    int found = 0;
    for (int menu = 0; menu < kMenuCount; ++menu) {
        const int count = menuRowCount(menu);
        for (int i = 0; i < count; ++i) {
            const MenuRow& row = menuRow(menu, i);
            if (row.separator || !row.text) continue;
            if (!containsFolded(row.text, needle)) continue;
            if (found < capacity) {
                out[found].menu = menu;
                out[found].index = i;
            }
            ++found;
        }
    }
    return found;
}

// Moves the camera back along its own view direction until the selection fits in
// the frame. Yaw and pitch are left alone: framing something is not the same
// request as looking at it from a new angle, and the user still has the orbit
// gizmo for that.
void Editor::frameSelection(PhysicsWorld& world, Camera& camera) {
    Vec3 centre{0.0f, 0.0f, 0.0f};
    float radius = 0.0f;
    int counted = 0;
    for (int index : selectedBodies_) {
        const RigidBody* body = world.find(index);
        if (!body) continue;
        centre += body->position;
        radius = std::fmax(radius, body->boundRadius);
        ++counted;
    }
    if (counted == 0) return;

    centre = centre / (float)counted;
    const float halfFov = camera.fovDeg * 0.5f * kPi / 180.0f;
    // Half the frame's height has to cover the radius, plus a margin so the object
    // does not sit against the edge.
    const float distance = radius / std::fmax(0.05f, std::tan(halfFov)) * 1.6f + radius;
    camera.position = centre - camera.forward() * distance;
    viewRay_.valid = false;
}

std::string Editor::projectDir() const {
    // The scene path is "<projectDir>/scene.txt", so the directory is known without
    // asking the application for the project.
    const size_t slash = scenePath_.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : scenePath_.substr(0, slash);
}

void Editor::openModal(Modal dialog) {
    modal_ = dialog;
    modalRow_ = 0;
    modalFiles_.clear();
    switch (dialog) {
    case Modal::NewScene:
        modalField_.clear();
        break;
    case Modal::OpenScene:
        // Every scene file in the project, so the dialog is a choice rather than a
        // spelling test.
        modalFiles_ = fs::listFilesByExtension(projectDir(), {".txt"});
        modalField_.set("scene.txt");
        break;
    case Modal::SaveAs:
        modalField_.set("scene.txt");
        break;
    case Modal::BuildSettings:
        modalField_.set(str::join(projectDir(), "build").c_str());
        break;
    case Modal::SaveFirst:
    case Modal::None:
        break;
    }
}

// Three of these commands throw the current scene away. With unsaved work in it,
// the question comes first - and it is the whole point of asking: there is no
// autosave here, and "New Scene" is one stray click away from an afternoon.
void Editor::requestSceneChange(Modal dialog, PhysicsWorld& world) {
    (void)world;
    if (!sceneDirty_) {
        openModal(dialog);
        return;
    }
    switch (dialog) {
    case Modal::NewScene: pendingAction_ = Pending::NewScene; break;
    case Modal::OpenScene: pendingAction_ = Pending::OpenScene; break;
    default: pendingAction_ = Pending::NewScene; break;
    }
    openModal(Modal::SaveFirst);
}



void Editor::runAction(Action action, int param, PhysicsWorld& world, Camera& camera,
                       Window& window) {
    switch (action) {
    case Action::None:
        break;
    case Action::NewScene:
        requestSceneChange(Modal::NewScene, world);
        break;
    case Action::OpenScene:
        requestSceneChange(Modal::OpenScene, world);
        break;
    case Action::Save:
        saveScene(world);
        break;
    case Action::SaveAs:
        openModal(Modal::SaveAs);
        break;
    case Action::NewProject:
    case Action::OpenProject: {
        // Unsaved work first, then back to the launcher, which owns the project
        // list and the project settings.
        const ShellRequest leave =
            action == Action::NewProject ? ShellRequest::NewProject : ShellRequest::OpenProject;
        if (sceneDirty_) {
            pendingAction_ = Pending::LeaveProject;
            pendingShell_ = leave;
            openModal(Modal::SaveFirst);
        } else {
            shellRequest_ = leave;
        }
        break;
    }
    case Action::SaveProject:
        shellRequest_ = ShellRequest::SaveProject;
        break;
    case Action::BuildSettings:
        openModal(Modal::BuildSettings);
        break;
    case Action::BuildRun:
        // No dialog: the path is the one the dialog would have shown, and "Build
        // and Run" in Unity is the same button as Build plus a launch.
        shellRequest_ = ShellRequest::BuildRun;
        break;
    case Action::Reload:
        assets_.refresh();
        loadScene(world);
        break;
    case Action::Exit:
        window.requestClose();
        break;
    case Action::Duplicate: {
        const RigidBody* source = world.find(activeBody_);
        if (!source) break;
        // Into a local first: push_back may reallocate the vector the source
        // points into, and copying a reference out of the vector it reallocates is
        // undefined even when it "usually" works.
        RigidBody copy = *source;
        copy.asleep = false;
        copy.sleepTime = 0.0f;
        copy.velocity = {};
        copy.angularVelocity = {};
        world.bodies.push_back(copy);
        markSceneDirty();
        const int index = (int)world.bodies.size() - 1;
        activeBody_ = index;
        selectedBodies_.assign(1, index);
        world.bodies.back().wake();
        break;
    }
    case Action::Delete:
        deleteSelection(world);
        break;
    case Action::ClearSelection:
        selectedBodies_.clear();
        activeBody_ = -1;
        break;
    case Action::Wake:
        if (RigidBody* body = world.find(activeBody_)) body->wake();
        break;
    case Action::ToggleFavorite:
        if (RigidBody* body = world.find(activeBody_)) {
            body->favorite = !body->favorite;
            markSceneDirty();
        }
        break;
    case Action::TogglePanel:
        if (param >= 0 && param < kPanelCount) {
            Panel& panel = panels_[param];
            panel.open = !panel.open;
            // Reopening into an edge another open panel already holds would put two
            // panels on one edge, so this one comes back floating instead - with the
            // size and place it had when it was closed.
            if (panel.open && panel.zone != PanelZone::Float) {
                for (int i = 0; i < kPanelCount; ++i)
                    if (i != param && panels_[i].open && panels_[i].zone == panel.zone) {
                        panel.zone = PanelZone::Float;
                        break;
                    }
            }
        }
        break;
    case Action::Grid:
        showGrid_ = !showGrid_;
        break;
    case Action::Vectors:
        showVectors_ = !showVectors_;
        break;
    case Action::Fov:
        if (param >= 0 && param < 3) {
            perspectiveIndex_ = param;
            camera.fovDeg = kPerspectiveValues[param];
        }
        break;
    case Action::FrameSelection:
        frameSelection(world, camera);
        break;
    case Action::ResetCamera:
        camera = homeCamera_;
        viewRay_.valid = false;
        break;
    case Action::Shape:
        BodyFactory::spawnInFrontOf(world, (Shape)param, camera, 1.0f);
        break;
    case Action::Model: {
        const std::vector<std::string>& models = assets_.files();
        if (param < 0 || param >= (int)models.size()) break;
        if (assets_.import(models[(size_t)param], world, camera, meshes_, activeBody_) < 0)
            Log::error("Import failed: %s", models[(size_t)param].c_str());
        break;
    }
    case Action::Controls:
        helpPage_ = 1;
        break;
    case Action::About:
        helpPage_ = 2;
        break;
    case Action::CloseOverlay:
        closeOverlays();
        break;
    case Action::LayerSelect:
        activeBody_ = param;
        selectedBodies_.assign(1, param);
        break;
    case Action::LayerVisible:
    case Action::LayerLocked:
        if (RigidBody* body = world.find(param)) {
            if (action == Action::LayerVisible) body->visible = !body->visible;
            else body->locked = !body->locked;
        }
        break;
    }
}

// The top bar: menus on the left, the transport in the middle, and the search /
// layers / help / reload cluster on the right, which is where the sketch has it.
//
// Only the parts that decide what is open are handled here. The dropdowns
// themselves are drawn in drawTopBarOverlay at the end of the frame, because they
// hang over the panels below; a press that opens one must not also close it, and
// nothing may change after the frame has been drawn - with on-demand rendering a
// change nobody redraws stays on screen until something else wakes the window.
void Editor::drawTopBar(UiRenderer& ui, PhysicsWorld& world, Camera& camera, Window& window,
                        int fbWidth) {
    buildMenuRows();
    overlayOpenedThisFrame_ = false;

    const Rect bar = layout_.menuBar;
    ui.fillRect(bar.x, bar.y, bar.width, bar.height, theme::background);
    ui.fillRect(bar.x, bar.y + bar.height - 1.0f, bar.width, 1.0f, theme::separator);

    const float buttonY = bar.y + (bar.height - kBarButtonHeight) * 0.5f;

    // --- left: the menus ---
    float menuX = 6.0f;
    for (int i = 0; i < kMenuCount; ++i) {
        const char* label = menuCaption(i);
        const float width = ui.textWidth(label) + 18.0f;
        const Rect hit{menuX, buttonY, width, kBarButtonHeight};
        const bool hovered = ui_.hovered(hit);

        // The open one keeps its highlight: an open menu that only looks open
        // while the pointer happens to be over it is not open, it is flickering.
        const bool open = openMenu_ == i;
        if (open)
            ui.fillRect(hit.x, hit.y, hit.width, hit.height, theme::buttonPressed);
        else if (hovered)
            ui.fillRect(hit.x, hit.y, hit.width, hit.height, theme::buttonHover);
        ui.text(hit.x + 9.0f,
                hit.y + (hit.height - theme::textLineHeight) * 0.5f, label,
                open ? theme::text : theme::textDim);

        if (hovered && ui_.pointerPressed()) {
            if (openMenu_ == i) {
                closeOverlays();
            } else {
                // Opening one closes the others: they are all anchored to this
                // bar, and two open at once is two overlapping dropdowns. The
                // search text goes with it - a dropdown that stays open behind a
                // menu is a list of matches for a query nobody is looking at.
                closeOverlays();
                toolbarSearch_.clear();
                openMenu_ = i;
                overlayOpenedThisFrame_ = true;
            }
        }
        menuX += width;
    }

    // --- tools, next to the menus, the way Unity puts them ---
    //
    // The same five tools the viewport column carries, one click away without the
    // pointer having to go into the scene first. Same Tool value either way, so
    // the two are two handles on one setting, not two settings: the bar shows the
    // active one lit, and so does the column.
    static const Tool kTools[5] = {Tool::Select, Tool::Move, Tool::Rotate, Tool::Scale,
                                   Tool::Pan};
    float toolX = menuX + 10.0f;
    for (int i = 0; i < 5; ++i) {
        const Rect rect{toolX, buttonY, kBarButtonHeight, kBarButtonHeight};
        const bool on = tool_ == kTools[i];
        if (ui_.button(ui, kTopBarTool + i, rect.x, rect.y, rect.width, rect.height, "", on))
            tool_ = kTools[i];
        drawToolIcon(ui, rect, kTools[i], on);
        toolX += kBarButtonHeight + kBarGap;
    }
    // A hairline between the tools and the rest of the bar: without it the group
    // reads as part of the menu captions.
    ui.fillRect(toolX + 2.0f, bar.y + 5.0f, 1.0f, bar.height - 10.0f, theme::separator);

    // --- centre: transport, like a media player ---
    const float groupWidth = kBarButtonHeight * 3.0f + kBarGap * 2.0f;
    float tx = (float)fbWidth * 0.5f - groupWidth * 0.5f;
    auto transport = [&](int id, Glyph glyph, bool on) {
        const Rect rect{tx, buttonY, kBarButtonHeight, kBarButtonHeight};
        const bool pressed = ui_.button(ui, id, rect.x, rect.y, rect.width, rect.height, "", on);
        drawGlyph(ui, rect, glyph, on ? theme::accent : theme::textDim);
        tx += kBarButtonHeight + kBarGap;
        return pressed;
    };
    if (transport(kTransportPlay, Glyph::Play, playMode_ && playing_)) {
        if (!playMode_) enterPlayMode(world);
        else playing_ = true;
    }
    if (transport(kTransportPause, Glyph::Pause, playMode_ && !playing_)) {
        if (playMode_) {
            playing_ = !playing_;
            Log::info(playing_ ? text("log.resumed") : text("log.paused_"));
        }
    }
    if (transport(kTransportStop, Glyph::Stop, false)) {
        if (playMode_) exitPlayMode(world);
    }

    // --- right: counter, search, help, layers, reload ---
    //
    // Laid out from the right edge inwards, so the cluster can never grow into the
    // transport on a narrow window.
    float x = (float)fbWidth - 6.0f;

    const Rect reload{x - kBarButtonHeight, buttonY, kBarButtonHeight, kBarButtonHeight};
    if (ui_.button(ui, kToolbarReload, reload.x, reload.y, reload.width, reload.height, ""))
        runAction(Action::Reload, 0, world, camera, window);
    drawGlyph(ui, reload, Glyph::Reload, theme::textDim);
    x = reload.x - kBarGap;

    const char* layersLabel = text("toolbar.layers");
    const float layersWidth = ui.textWidth(layersLabel) + 18.0f;
    const Rect layers{x - layersWidth, buttonY, layersWidth, kBarButtonHeight};
    layersButton_ = layers;
    if (ui_.button(ui, kToolbarLayers, layers.x, layers.y, layers.width, layers.height, "",
                   layersOpen_))
        layersOpen_ = !layersOpen_;
    ui.text(layers.x + 9.0f, layers.y + (layers.height - theme::textLineHeight) * 0.5f,
            layersLabel, layersOpen_ ? theme::text : theme::textDim);
    x = layers.x - kBarGap;

    const Rect search{x - kSearchWidth, buttonY, kSearchWidth, kBarButtonHeight};
    searchField_ = search;
    const bool searchHovered = ui_.hovered(search);
    // Pumped here, drawn here: the field belongs to the bar, and the results list
    // under it is drawn with the other overlays at the end of the frame.
    ui_.pumpTextField(window, toolbarSearch_, searchHovered);
    ui.fillRect(search.x, search.y, search.width, search.height,
                toolbarSearch_.editing ? theme::accent.scaled(0.35f) : theme::field);
    const Rect searchGlyph{search.x + 3.0f, search.y, kBarButtonHeight - 4.0f, kBarButtonHeight};
    drawGlyph(ui, searchGlyph, Glyph::Search, theme::textDim);
    const char* searchText =
        toolbarSearch_.empty() ? text("toolbar.search") : toolbarSearch_.text();
    ui.text(search.x + kBarButtonHeight, search.y + (search.height - theme::textLineHeight) * 0.5f,
            searchText, toolbarSearch_.empty() ? theme::textDim : theme::text);
    if (toolbarSearch_.editing)
        ui.fillRect(search.x + kBarButtonHeight + ui.textWidth(toolbarSearch_.text()) + 1.0f,
                    search.y + 3.0f, 1.5f, search.height - 6.0f, theme::accent);
    x = search.x - kBarGap;

    const Rect help{x - kBarButtonHeight, buttonY, kBarButtonHeight, kBarButtonHeight};
    if (ui_.button(ui, kToolbarHelp, help.x, help.y, help.width, help.height, ""))
        runAction(helpPage_ == 1 ? Action::CloseOverlay : Action::Controls, 0, world, camera,
                  window);
    ui.text(help.x + (help.width - ui.textWidth("?")) * 0.5f,
            help.y + (help.height - theme::textLineHeight) * 0.5f, "?",
            helpPage_ == 1 ? theme::text : theme::textDim);
    x = help.x - kBarGap;

    // "3 / 12" - how much is selected out of how much there is. The two numbers
    // that answer "did I select the thing I meant to".
    char counter[32];
    std::snprintf(counter, sizeof(counter), "%d / %d", (int)selectedBodies_.size(),
                  (int)world.bodies.size());
    const float counterWidth = ui.textWidth(counter);
    if (x - counterWidth > menuX + 20.0f)
        ui.text(x - counterWidth, buttonY + (kBarButtonHeight - theme::textLineHeight) * 0.5f,
                counter, theme::textDim);
}

} // namespace s3d
