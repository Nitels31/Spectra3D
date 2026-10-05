#include "Editor.hpp"

#include "../core/Log.hpp"
#include "../scene/SceneSerializer.hpp"
#include "../ui/Theme.hpp"
#include "Picking.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace s3d {
namespace {

constexpr float kMinViewportSize = 120.0f;
// "Scene" strip above the viewport, and the vertical tool column inside it.
constexpr float kSceneHeaderHeight = 26.0f;
constexpr float kToolStripWidth = 34.0f;
// Console height under the viewport and the inspector when it is docked there.
constexpr float kConsoleHeight = 156.0f;
// Height of a panel's title bar: the handle it is dragged by. Every panel uses the
// same one, so a grip always sits in the same place on all of them.
constexpr float kPanelHeaderHeight = 24.0f;
// Fallback when no project is open: next to the binary.
const char* const kDefaultSceneFile = "scene.txt";

// Navigation balls: camera orientation presets, offsets from the widget centre.
struct NavBall {
    float dx, dy;
    float yaw, pitch;
};

const NavBall kNavBalls[6] = {
    {0.0f, -30.0f, 0.0f, -1.55f},      // look down +Y
    {0.0f, 30.0f, 0.0f, 1.55f},       // look up -Y
    {30.0f, 8.0f, -kPi * 0.5f, 0.0f}, // +X
    {-30.0f, 8.0f, kPi * 0.5f, 0.0f}, // -X
    {-26.0f, 26.0f, kPi, 0.0f},       // +Z
    {26.0f, -26.0f, 0.0f, 0.0f},      // -Z
};

} // namespace

Editor::Editor(MeshRegistry& meshes, i18n::Localizer& localizer)
    : meshes_(meshes), loc_(localizer) {
    filter_.id = kFilterField;
    toolbarSearch_.id = kToolbarSearch;
    // Seeded from the theme metrics; the edges are the only thing that changes
    // them afterwards.
    leftWidth_ = theme::leftDockWidth;
    rightWidth_ = theme::rightDockWidth;
    bottomHeight_ = kConsoleHeight;
    panels_[(int)PanelId::Hierarchy] = Panel{PanelZone::Left, Rect{}};
    panels_[(int)PanelId::Inspector] = Panel{PanelZone::Right, Rect{}};
    panels_[(int)PanelId::Console] = Panel{PanelZone::Bottom, Rect{}};
    // Somewhere sensible to drop the first floating panel, so undocking never
    // opens one off screen or one pixel wide.
    panels_[(int)PanelId::Inspector].floatRect = Rect{520.0f, 120.0f, 300.0f, 320.0f};
    panels_[(int)PanelId::Hierarchy].floatRect = Rect{60.0f, 120.0f, 260.0f, 300.0f};
    panels_[(int)PanelId::Console].floatRect = Rect{300.0f, 380.0f, 560.0f, 160.0f};
}

bool Editor::consumeSingleStep() {
    const bool requested = singleStepRequested_;
    singleStepRequested_ = false;
    return requested;
}

// Where a panel in `zone` sits this frame. A zone nobody occupies is an empty
// rect at the origin, so a panel that is floating does not keep an invisible claim
// on the edge it came from.
Rect Editor::zoneRect(PanelZone zone) const {
    if (zone == PanelZone::Float) return Rect{};

    const float width = (float)fbWidth_;
    const float height = (float)fbHeight_;
    const float top = theme::menuBarHeight;

    // Occupancy, not identity: an edge is only as tall as it can be because of
    // what else is docked. Asking "is anything in the bottom strip" is what keeps
    // a right dock from running under the console. Closed panels are not there.
    bool leftTaken = false, bottomTaken = false;
    for (int i = 0; i < kPanelCount; ++i) {
        if (!panels_[i].open) continue;
        if (panels_[i].zone == PanelZone::Left) leftTaken = true;
        else if (panels_[i].zone == PanelZone::Bottom) bottomTaken = true;
    }

    const float bottomY = height - (bottomTaken ? bottomHeight_ : 0.0f);
    switch (zone) {
    case PanelZone::Left:
        return Rect{0.0f, top, leftWidth_, bottomY - top};
    case PanelZone::Right:
        return Rect{width - rightWidth_, top, rightWidth_, bottomY - top};
    case PanelZone::Bottom: {
        // Under the right dock as well, the way it always was: the console is the
        // bottom of the window, not the bottom of the middle column.
        const float x = leftTaken ? leftWidth_ : 0.0f;
        return Rect{x, bottomY, width - x, bottomHeight_};
    }
    case PanelZone::Float:
        break;
    }
    return Rect{};
}

Rect Editor::panelRect(PanelId id) const {
    const Panel& panel = panels_[(int)id];
    if (!panel.open) return Rect{};
    return panel.zone == PanelZone::Float ? panel.floatRect : zoneRect(panel.zone);
}

Rect Editor::panelHeader(PanelId id) const {
    const Rect panel = panelRect(id);
    return Rect{panel.x, panel.y, panel.width, kPanelHeaderHeight};
}

// The X in a panel's title bar. Both the drawing and the drag test ask the same
// rectangle, so a click that closes a panel can never also start moving it.
Rect Editor::panelCloseRect(PanelId id) const {
    const Rect header = panelHeader(id);
    return Rect{header.x + header.width - 22.0f, header.y + 2.0f, 20.0f, 20.0f};
}

const char* Editor::panelTitle(PanelId id) const {
    switch (id) {
    case PanelId::Hierarchy: return text("tab.hierarchy");
    case PanelId::Inspector: return text("tab.inspector");
    case PanelId::Console: return text("tab.console");
    case PanelId::PanelCount: break;
    }
    return "";
}

// Which zone the pointer is over. The edges win over the bottom strip, and the
// middle of the scene means "float": dropping a panel there is the only way to
// get one off the edges, exactly as dragging a dock out of a Unity window does.
PanelZone Editor::zoneAt(float x, float y) const {
    const float edge = std::fmin(200.0f, (float)fbWidth_ * 0.25f);
    if (x <= edge) return PanelZone::Left;
    if (x >= (float)fbWidth_ - edge) return PanelZone::Right;

    const float strip = std::fmin(160.0f, (float)fbHeight_ * 0.28f);
    if (y >= (float)fbHeight_ - strip) return PanelZone::Bottom;
    return PanelZone::Float;
}

void Editor::commitPanelDrop() {
    if (dragPanel_ < 0) return;
    Panel& moving = panels_[dragPanel_];
    const PanelZone target = dropZone_;

    if (target == PanelZone::Float) {
        // Kept inside the window: a panel dragged half off screen is a panel the
        // user has to drag back to find.
        Rect rect = dragRect_;
        rect.width = std::fmin(rect.width, (float)fbWidth_ - 120.0f);
        rect.height = std::fmin(rect.height, (float)fbHeight_ - 80.0f);
        rect.x = clampf(rect.x, 0.0f, std::fmax(0.0f, (float)fbWidth_ - rect.width));
        rect.y = clampf(rect.y, (float)theme::menuBarHeight,
                        std::fmax(0.0f, (float)fbHeight_ - rect.height));
        moving.zone = PanelZone::Float;
        moving.floatRect = rect;
    } else {
        // Whoever is on the target edge goes where the dragged panel came from -
        // a swap, which is what makes the edges feel like three places and not
        // three slots.
        for (int i = 0; i < kPanelCount; ++i) {
            if (i == dragPanel_ || panels_[i].zone != target) continue;
            if (moving.zone == PanelZone::Float) panels_[i].floatRect = moving.floatRect;
            panels_[i].zone = moving.zone;
        }
        moving.zone = target;
    }
}

void Editor::handlePanelDrag(PhysicsWorld& world) {
    const float px = ui_.pointerX();
    const float py = ui_.pointerY();

    // --- resizing an edge, which is the other half of "move the panels" -----
    if (resizePanel_ >= 0) {
        if (ui_.pointerDown()) {
            const PanelZone zone = panels_[resizePanel_].zone;
            if (zone == PanelZone::Left)
                leftWidth_ = clampf(resizeStartSize_ + (px - resizeStartPointer_), 180.0f,
                                    520.0f);
            else if (zone == PanelZone::Right)
                rightWidth_ = clampf(resizeStartSize_ - (px - resizeStartPointer_), 180.0f,
                                     520.0f);
            else if (zone == PanelZone::Bottom)
                bottomHeight_ = clampf(resizeStartSize_ - (py - resizeStartPointer_), 80.0f,
                                       (float)fbHeight_ * 0.7f);
        } else {
            resizePanel_ = -1;
        }
        return;
    }
    if (ui_.pointerPressed()) {
        for (int i = 0; i < kPanelCount; ++i) {
            if (!panels_[i].open) continue;
            // The X closes; anything else in the header drags.
            if (ui_.hovered(panelCloseRect((PanelId)i))) continue;
            if (ui_.hovered(panelHeader((PanelId)i))) {
                dragPanel_ = i;
                const Rect header = panelHeader((PanelId)i);
                dragGrabX_ = px - header.x;
                dragGrabY_ = py - header.y;
                dragRect_ = panelRect((PanelId)i);
                dropZone_ = panels_[i].zone;
                return;
            }
        }
    }
    if (dragPanel_ < 0) {
        // Nothing grabbed: a press within a few pixels of a dock's inner edge
        // starts a resize instead.
        constexpr float kGrip = 5.0f;
        for (int i = 0; i < kPanelCount; ++i) {
            const PanelZone zone = panels_[i].zone;
            if (zone == PanelZone::Float || !panels_[i].open) continue;
            const Rect panel = panelRect((PanelId)i);
            const bool inside = px >= panel.x && px <= panel.x + panel.width &&
                                py >= panel.y + kPanelHeaderHeight && py <= panel.y + panel.height;
            if (!inside) continue;
            if (zone == PanelZone::Left && std::fabs(px - (panel.x + panel.width)) <= kGrip) {
                resizePanel_ = i;
            } else if (zone == PanelZone::Right &&
                       std::fabs(px - panel.x) <= kGrip) {
                resizePanel_ = i;
            } else if (zone == PanelZone::Bottom &&
                       std::fabs(py - panel.y) <= kGrip) {
                resizePanel_ = i;
            }
            if (resizePanel_ >= 0) {
                resizeStartPointer_ = (zone == PanelZone::Bottom) ? py : px;
                resizeStartSize_ = zone == PanelZone::Left  ? leftWidth_
                                 : zone == PanelZone::Right ? rightWidth_
                                                             : bottomHeight_;
                return;
            }
        }
        return;
    }

    // --- dragging ---------------------------------------------------------
    dragRect_.x = px - dragGrabX_;
    dragRect_.y = py - dragGrabY_;
    dropZone_ = zoneAt(px, py);
    if (ui_.pointerReleased()) {
        commitPanelDrop();
        dragPanel_ = -1;
    }
    (void)world;
}

// The grip in a panel's header, and the shadow under a floating one. Drawn by the
// editor rather than by each panel so all three handles look the same and only
// one thing has to change if that ever stops being true.
void Editor::drawPanelChrome(UiRenderer& ui, PanelId id) {
    const Rect header = panelHeader(id);
    const Panel& panel = panels_[(int)id];
    const bool floating = panel.zone == PanelZone::Float;

    // Six dots in two columns: the grip every draggable window has.
    for (int i = 0; i < 6; ++i) {
        const float x = header.x + 7.0f + (float)(i % 2) * 4.0f;
        const float y = header.y + header.height * 0.5f - 4.0f + (float)(i / 2) * 4.0f;
        ui.fillRect(x, y, 1.5f, 1.5f, theme::textDim.scaled(0.7f));
    }

    // The X that closes the panel. Closing it is one click, and the View menu
    // brings it back, so "this panel is in the way" is not a layout problem.
    const Rect close = panelCloseRect(id);
    const bool closeHovered = ui_.hovered(close);
    const Color tint = closeHovered ? theme::error : theme::textDim;
    for (int i = 0; i < 5; ++i) {
        const float o = 3.0f + (float)i * 2.6f;
        ui.fillRect(close.x + o, close.y + o, 1.8f, 1.8f, tint);
        ui.fillRect(close.x + 12.0f - o + 2.0f, close.y + o, 1.8f, 1.8f, tint);
    }
    if (closeHovered && ui_.pointerPressed()) {
        panels_[(int)id].open = false;
        return;
    }

    if (floating) {
        const Rect rect = panelRect(id);
        // A border and a one-pixel shadow: without them a floating panel reads as
        // a hole in the layout rather than a window over the scene. Edges only -
        // filling the panel would paint over everything it just drew.
        ui.fillRect(rect.x, rect.y, rect.width, 1.0f, theme::separator);
        ui.fillRect(rect.x, rect.y + rect.height - 1.0f, rect.width, 1.0f, theme::separator);
        ui.fillRect(rect.x, rect.y, 1.0f, rect.height, theme::separator);
        ui.fillRect(rect.x + rect.width - 1.0f, rect.y, 1.0f, rect.height,
                    theme::separator);
        ui.fillRect(rect.x + 2.0f, rect.y + rect.height, rect.width, 2.0f,
                    Color{0.0f, 0.0f, 0.0f, 0.35f});
        ui.fillRect(rect.x + rect.width, rect.y + 2.0f, 2.0f, rect.height,
                    Color{0.0f, 0.0f, 0.0f, 0.35f});
    }
}

// Where a panel would land: the whole target zone washed with the accent, or the
// panel itself following the pointer when it is about to float.
void Editor::drawPanelDragFeedback(UiRenderer& ui) {
    if (dragPanel_ >= 0) {
        if (dropZone_ == PanelZone::Float) {
            const Rect ghost = dragRect_;
            ui.fillRect(ghost.x, ghost.y, ghost.width, ghost.height,
                        Color{0.10f, 0.13f, 0.18f, 0.85f});
            ui.fillRect(ghost.x, ghost.y, ghost.width, kPanelHeaderHeight, theme::accentDim);
            ui.text(ghost.x + 10.0f, ghost.y + 5.0f,
                    panelTitle((PanelId)dragPanel_), theme::text);
            ui.fillRect(ghost.x, ghost.y, ghost.width, 1.0f, theme::accent);
        } else {
            const Rect zone = zoneRect(dropZone_);
            ui.fillRect(zone.x, zone.y, zone.width, zone.height,
                        Color{0.30f, 0.55f, 0.97f, 0.16f});
            ui.fillRect(zone.x, zone.y, zone.width, 1.0f, theme::accent);
            ui.fillRect(zone.x, zone.y + zone.height - 1.0f, zone.width, 1.0f, theme::accent);
        }
    }
    // The resize handle: a two-pixel column on the edge that takes the drag, so it
    // is findable without a hint.
    if (resizePanel_ >= 0) {
        const PanelZone zone = panels_[resizePanel_].zone;
        const Rect panel = panelRect((PanelId)resizePanel_);
        const bool hovered = std::fabs(ui_.pointerX() - (zone == PanelZone::Right ? panel.x
                                                                  : panel.x + panel.width)) < 6.0f;
        const Color tint = hovered ? theme::accent : theme::separator;
        if (zone == PanelZone::Bottom)
            ui.fillRect(panel.x, panel.y - 1.0f, panel.width, 2.0f, tint);
        else if (zone == PanelZone::Left)
            ui.fillRect(panel.x + panel.width - 1.0f, panel.y, 2.0f, panel.height, tint);
        else
            ui.fillRect(panel.x, panel.y, 2.0f, panel.height, tint);
    }
}

void Editor::recomputeLayout(int fbWidth, int fbHeight) {
    fbWidth_ = fbWidth;
    fbHeight_ = fbHeight;
    const float width = (float)fbWidth;
    const float height = (float)fbHeight;

    layout_.menuBar = Rect{0.0f, 0.0f, width, theme::menuBarHeight};

    // Which edges are occupied decides the scene: a floating inspector gives its
    // space back to the viewport, and an empty left edge moves the scene to x=0.
    bool leftTaken = false, rightTaken = false, bottomTaken = false;
    for (int i = 0; i < kPanelCount; ++i) {
        if (!panels_[i].open) continue;
        switch (panels_[i].zone) {
        case PanelZone::Left: leftTaken = true; break;
        case PanelZone::Right: rightTaken = true; break;
        case PanelZone::Bottom: bottomTaken = true; break;
        case PanelZone::Float: break;
        }
    }

    const float sceneLeft = leftTaken ? leftWidth_ : 0.0f;
    const float sceneRight = rightTaken ? rightWidth_ : 0.0f;
    const float columnTop = theme::menuBarHeight;
    const float sceneBottom = bottomTaken ? height - bottomHeight_ : height;

    const float headerH = kSceneHeaderHeight;
    const float viewX = sceneLeft;
    const float viewW = std::fmax(kMinViewportSize, width - sceneLeft - sceneRight);
    layout_.sceneHeader = Rect{viewX, columnTop, viewW, headerH};
    layout_.viewport = Rect{viewX, columnTop + headerH, viewW,
                            std::fmax(kMinViewportSize, sceneBottom - columnTop - headerH)};
    layout_.viewportTools = Rect{layout_.viewport.x + 4.0f, layout_.viewport.y + 4.0f,
                                 kToolStripWidth, 0.0f};
    layout_.viewportTools.height = std::max(0.0f, layout_.viewport.height - 8.0f);
}

void Editor::pruneSelection(const PhysicsWorld& world) {
    const int count = (int)world.bodies.size();
    if (activeBody_ >= count) activeBody_ = -1;
    selectedBodies_.erase(
        std::remove_if(selectedBodies_.begin(), selectedBodies_.end(),
                       [&](int index) { return index < 0 || index >= count; }),
        selectedBodies_.end());
}

void Editor::deleteSelection(PhysicsWorld& world) {
    std::sort(selectedBodies_.rbegin(), selectedBodies_.rend());
    for (int index : selectedBodies_) {
        if (index < 0 || index >= (int)world.bodies.size()) continue;
        // A locked body stays. "Locked" has to mean something the editor enforces,
        // and the one thing a lock is for is that a stray Delete cannot throw work
        // away.
        if (world.bodies[(size_t)index].locked) {
            Log::info("Body %d is locked", index);
            continue;
        }
Log::info("Deleted body %d", index);
        world.remove(index);
        markSceneDirty();
    }
    selectedBodies_.clear();
    activeBody_ = -1;
}

void Editor::updateViewRay(const Camera& camera) {
    if (viewRay_.valid) return;
    camera.basis(viewRay_.forward, viewRay_.right, viewRay_.up);
    const float tanY = std::tan(camera.fovDeg * kPi / 360.0f);
    viewRay_.tanY = tanY;
    viewRay_.tanX = tanY * layout_.viewport.width / layout_.viewport.height;
    viewRay_.valid = true;
}

Vec3 Editor::viewportRay(const Camera& camera) {
    updateViewRay(camera);
    return picking::rayFromViewport(ui_.pointerX(), ui_.pointerY(), layout_.viewport,
                                    viewRay_.forward, viewRay_.right, viewRay_.up,
                                    viewRay_.tanX, viewRay_.tanY);
}

void Editor::appendDecimal(char* out, int& at, unsigned long long value, int minDigits) {
    char digits[24];
    int count = 0;
    do {
        digits[count++] = (char)('0' + (int)(value % 10ull));
        value /= 10ull;
    } while (value != 0);

    char padded[24];
    const int pad = minDigits > count ? minDigits - count : 0;
    for (int i = 0; i < pad; ++i) padded[i] = '0';
    for (int i = 0; i < count; ++i) padded[pad + i] = digits[count - 1 - i];
    for (int i = 0; i < pad + count; ++i) out[at++] = padded[i];
}

void Editor::formatIndexed(char* out, size_t size, const char* name, char separator,
                           int index) {
    const size_t nameLength = std::strlen(name);
    // Longest possible output is name + separator + sign + 10 digits + NUL.
    if (size < nameLength + 13u) {
        if (separator)
            std::snprintf(out, size, "%s%c%02d", name, separator, index);
        else
            std::snprintf(out, size, "%s%02d", name, index);
        return;
    }

    std::memcpy(out, name, nameLength);
    int at = (int)nameLength;
    if (separator) out[at++] = separator;
    // The '0' flag pads between the sign and the digits, so a negative value
    // never reaches the two digit minimum.
    if (index < 0) {
        out[at++] = '-';
        appendDecimal(out, at, (unsigned long long)(-(long long)index), 1);
    } else {
        appendDecimal(out, at, (unsigned long long)index, 2);
    }
    out[at] = '\0';
}

void Editor::formatPrefixed(char* out, size_t size, const char* prefix, const char* value) {
    const size_t prefixLength = std::strlen(prefix);
    const size_t valueLength = std::strlen(value);
    if (size < prefixLength + valueLength + 1u) {
        std::snprintf(out, size, "%s%s", prefix, value);
        return;
    }
    std::memcpy(out, prefix, prefixLength);
    std::memcpy(out + prefixLength, value, valueLength);
    out[prefixLength + valueLength] = '\0';
}

void Editor::handleInput(PhysicsWorld& world, Camera& camera, Window& window, int fbWidth,
                         int fbHeight) {
    ui_.beginInput(window);
    viewRay_.valid = false;
    recomputeLayout(fbWidth, fbHeight);
    handleHotkeys(world, window);
    handlePanelDrag(world);
    handleViewport(world, camera, window);
}

void Editor::handleHotkeys(PhysicsWorld& world, Window& window) {
    // Deliberately empty of state-changing shortcuts.
    //
    // Play / Pause / Stop and save / load used to be bound to keys. Every one of
    // them mutates or discards the scene, and a stray keypress while typing or
    // working silently threw work away - stopping play mode restores the snapshot
    // taken when play began, so anything built during the run is gone.
    // Mode changes now only happen through the buttons in the menu bar.
    (void)world;
    (void)window;
}

bool Editor::tryNavigationGizmo(Camera& camera) {
    const Rect viewport = layout_.viewport;
    const float centerX = viewport.x + viewport.width - 52.0f;
    const float centerY = viewport.y + 52.0f;
    if (!ui_.pointerPressed()) return false;

    for (const NavBall& ball : kNavBalls) {
        const float dx = ui_.pointerX() - (centerX + ball.dx);
        const float dy = ui_.pointerY() - (centerY + ball.dy);
        if (dx * dx + dy * dy >= 144.0f) continue;
        camera.yaw = ball.yaw;
        camera.pitch = ball.pitch;
        viewRay_.valid = false;
        return true;
    }
    return false;
}

bool Editor::gizmoModeFor(Tool tool, gizmo::Mode& modeOut) const {
    switch (tool) {
    case Tool::Move: modeOut = gizmo::Mode::Move; return true;
    case Tool::Rotate: modeOut = gizmo::Mode::Rotate; return true;
    case Tool::Scale: modeOut = gizmo::Mode::Scale; return true;
    // Select picks bodies and Pan moves the camera; neither has handles.
    case Tool::Select:
    case Tool::Pan: return false;
    }
    return false;
}

bool Editor::tryGizmoGrip(PhysicsWorld& world, Camera& camera) {
    gizmo::Mode mode = gizmo::Mode::Move;
    if (!gizmoModeFor(tool_, mode)) return false;
    const RigidBody* body = world.find(activeBody_);
    if (!body) return false;
    // Same rule as Delete: a locked body shows no handles to grab, so the lock
    // cannot be argued with by dragging.
    if (body->locked) return false;

    const Rect viewport = layout_.viewport;
    const Vec3 rayOrigin = camera.position;
    const Vec3 rayDir = viewportRay(camera);
    const float pointerX = ui_.pointerX() - viewport.x;
    const float pointerY = ui_.pointerY() - viewport.y;

    dragAxis_ = mode == gizmo::Mode::Rotate
                    ? gizmo::pickRing(pointerX, pointerY, rayOrigin, rayDir, camera.position,
                                      viewRay_.forward, viewRay_.right, viewRay_.up,
                                      viewRay_.tanX, viewRay_.tanY, viewport, *body)
                    : gizmo::pickAxis(pointerX, pointerY, rayOrigin, rayDir, camera.position,
                                      viewRay_.forward, viewRay_.right, viewRay_.up,
                                      viewRay_.tanX, viewRay_.tanY, viewport, *body, mode);
    if (dragAxis_ < 0) return false;

    dragOrigin_ = body->position;
    Vec3 axes[3];
    gizmo::axesFor(*body, mode, axes);

    if (mode == gizmo::Mode::Rotate) {
        // Remember where the pointer sat on the ring, in the ring's own frame, so
        // the drag is a delta from that and not an absolute angle.
        dragStartOrientation_ = body->orientation;
        rotateStartAngle_ = pointerAngleOnRing(rayOrigin, rayDir, *body, axes[dragAxis_],
                                               pointerX, pointerY, camera);
        return true;
    }

    float rayT = 0.0f;
    picking::closestOnAxis(rayOrigin, rayDir, dragOrigin_, axes[dragAxis_],
                           dragStartAxisT_, rayT);
    return true;
}

// Where the pointer ray crosses the plane the ring lies in, as an angle around
// `axis`. Returns 0 when the ray is too close to parallel to that plane to give a
// meaningful answer, which leaves the body where it was instead of spinning it.
float Editor::pointerAngleOnRing(const Vec3& rayOrigin, const Vec3& rayDir,
                                 const RigidBody& body, const Vec3& axis, float pointerX,
                                 float pointerY, const Camera& camera) {
    const float denominator = rayDir.x * axis.x + rayDir.y * axis.y + rayDir.z * axis.z;
    if (std::fabs(denominator) < 1e-3f) return 0.0f;

    const Vec3 toPlane = body.position - rayOrigin;
    const float t = (toPlane.x * axis.x + toPlane.y * axis.y + toPlane.z * axis.z) / denominator;
    if (t <= 0.0f) return 0.0f;

    const Vec3 hit = rayOrigin + rayDir * t - body.position;
    Vec3 u, v;
    gizmo::ringFrame(axis, u, v);
    return std::atan2(hit.x * v.x + hit.y * v.y + hit.z * v.z,
                      hit.x * u.x + hit.y * u.y + hit.z * u.z);
}

void Editor::applyGizmoDrag(PhysicsWorld& world, Camera& camera) {
    RigidBody* body = world.find(activeBody_);
    if (!body) return;

    const Rect viewport = layout_.viewport;
    const Vec3 rayOrigin = camera.position;
    const Vec3 rayDir = viewportRay(camera);

    gizmo::Mode mode = gizmo::Mode::Move;
    if (!gizmoModeFor(tool_, mode)) return;

    Vec3 axes[3];
    gizmo::axesFor(*body, mode, axes);

    if (mode == gizmo::Mode::Rotate) {
        // The pointer's angle around the axis now, minus where it started: a delta
        // applied to the orientation the body had when the ring was grabbed.
        const float angle = pointerAngleOnRing(rayOrigin, rayDir, *body, axes[dragAxis_],
                                               ui_.pointerX() - viewport.x,
                                               ui_.pointerY() - viewport.y, camera);
        const float delta = angle - rotateStartAngle_;
        if (std::fabs(delta) < 1e-4f) return;
        body->orientation =
            (Quat::axisAngle(axes[dragAxis_], delta) * dragStartOrientation_).normalized();
        body->wake();
        return;
    }

    float axisT = 0.0f, rayT = 0.0f;
    picking::closestOnAxis(rayOrigin, rayDir, dragOrigin_, axes[dragAxis_], axisT, rayT);

if (mode == gizmo::Mode::Move) {
        Vec3 moved = dragOrigin_ + axes[dragAxis_] * (axisT - dragStartAxisT_);
        // Snap on all three axes, not just the dragged one: a half-way position
        // that rounds on the axis the hand is on and not on the others is the kind
        // of thing that looks like a bug and is.
        if (snapEnabled_) {
            constexpr float kSnap = 0.25f;
            moved.x = std::round(moved.x / kSnap) * kSnap;
            moved.y = std::round(moved.y / kSnap) * kSnap;
            moved.z = std::round(moved.z / kSnap) * kSnap;
        }
        body->position = moved;
    } else {
        float factor =
            std::fabs(dragStartAxisT_) < 1e-3f ? 1.0f : axisT / dragStartAxisT_;
        factor = clampf(factor, 0.05f, 20.0f);
        const float scaled = dragStartScale_ * factor;

        if (body->hasMesh()) {
            body->meshScale = clampf(scaled, 0.05f, 10.0f);
            const MeshAsset* mesh = meshes_.at(body->mesh);
            if (mesh) body->halfExtents = mesh->half * body->meshScale;
            body->configure(body->mass);
        } else if (body->shape == Shape::Sphere) {
            const float radius = clampf(scaled, 0.05f, 10.0f);
            body->halfExtents = {radius, radius, radius};
            body->configure(body->mass);
        } else {
            (&body->halfExtents.x)[dragAxis_] = clampf(scaled, 0.05f, 10.0f);
            body->configure(body->mass);
        }
    }
    body->wake();
}

// A five-pointed star, built from small squares along five rays.
//
// There is no star glyph in the atlas (ASCII and Cyrillic only) and the batcher
// cannot rotate a rectangle, so the rays are walked one dot at a time: five rays
// of four dots, which at row height is what a star has to look like anyway.
void Editor::drawStar(UiRenderer& ui, float cx, float cy, float radius, const Color& tint) {
    constexpr int kRays = 5;
    constexpr int kDots = 5;
    for (int ray = 0; ray < kRays; ++ray) {
        // Straight up first, then every 72 degrees clockwise.
        const float angle = -kPi * 0.5f + (float)ray * (2.0f * kPi / (float)kRays);
        for (int dot = 1; dot <= kDots; ++dot) {
            const float t = (float)dot / (float)kDots;
            ui.fillRect(cx + std::cos(angle) * radius * t - 0.8f,
                        cy + std::sin(angle) * radius * t - 0.8f, 1.7f, 1.7f, tint);
        }
    }
    ui.fillRect(cx - 1.2f, cy - 1.2f, 2.4f, 2.4f, tint);
}

void Editor::handleViewport(PhysicsWorld& world, Camera& camera, Window& window) {
    if (ui_.cameraCaptured()) {
        dragAxis_ = -1;
        return;
    }

const Rect viewport = layout_.viewport;
    // A press on the tool column or the "Scene" strip is a UI press, not a drag in
    // the scene: without this, clicking GIZ or M would also grab whatever is under
    // the pointer in the world.
    const Rect tools = layout_.viewportTools;
    const bool onChrome = ui_.hovered(tools) ||
                          ui_.hovered(layout_.sceneHeader);
    if (!ui_.hovered(viewport) || onChrome) {
        if (ui_.pointerReleased()) dragAxis_ = -1;
        return;
    }

    if (tryNavigationGizmo(camera)) return;

    // Pan is a viewport drag, not a UI press: start it here and keep feeding it
    // below, so the camera tracks the pointer for as long as the button is down.
    if (tool_ == Tool::Pan) {
        if (ui_.pointerPressed()) {
            panStartPointer_ = {ui_.pointerX(), ui_.pointerY(), 0.0f};
            panStartCamera_ = camera.position;
            // One screen pixel of drag per world unit at the focal plane, so the
            // scene follows the pointer at any zoom or field of view. The plane is
            // taken at whatever the user is looking at: the selection if there is
            // one, otherwise the origin.
            Vec3 reference{0.0f, 0.0f, 0.0f};
            if (const RigidBody* body = world.find(activeBody_)) reference = body->position;
            const float distance = (camera.position - reference).length();
            panPixelsPerUnit_ = std::fmax(0.001f, distance * 2.0f *
                                                        std::tan(camera.fovDeg * 0.5f * kPi / 180.0f) /
                                                        std::fmax(1.0f, viewport.height));
            viewRay_.valid = false;
            return;
        }
        if (ui_.pointerDown()) {
            Vec3 forward, right, up;
            camera.basis(forward, right, up);
            const float dx = ui_.pointerX() - panStartPointer_.x;
            const float dy = ui_.pointerY() - panStartPointer_.y;
            camera.position =
                panStartCamera_ - right * (dx * panPixelsPerUnit_) + up * (dy * panPixelsPerUnit_);
            viewRay_.valid = false;
            return;
        }
        return;
    }

    if (ui_.pointerPressed()) {
        if (tryGizmoGrip(world, camera)) {
            const RigidBody* body = world.find(activeBody_);
            gizmo::Mode mode = gizmo::Mode::Move;
            if (body && gizmoModeFor(tool_, mode) && mode == gizmo::Mode::Scale) {
                if (body->hasMesh()) dragStartScale_ = body->meshScale;
                else if (body->shape == Shape::Sphere) dragStartScale_ = body->halfExtents.x;
                else dragStartScale_ = (&body->halfExtents.x)[dragAxis_];
            }
            return;
        }
    }

if (ui_.pointerDown() && dragAxis_ >= 0 && world.find(activeBody_)) {
        applyGizmoDrag(world, camera);
        // Every frame the hand is moving the body is a frame the scene has changed:
        // this flag is what makes New Scene ask about the work before dropping it.
        markSceneDirty();
        if (ui_.pointerReleased()) dragAxis_ = -1;
        return;
    }
    if (ui_.pointerReleased() && dragAxis_ >= 0) {
        dragAxis_ = -1;
        return;
    }
    if (ui_.pointerPressed())
        handleSelectionClick(world, camera.position, viewportRay(camera));
}

void Editor::handleSelectionClick(PhysicsWorld& world, const Vec3& rayOrigin,
                                  const Vec3& rayDir) {
    const int hit = picking::pickBody(rayOrigin, rayDir, world);

    if (!ui_.ctrlHeld()) selectedBodies_.clear();
    if (hit < 0) {
        activeBody_ = -1;
        return;
    }

    if (ui_.ctrlHeld()) {
        const auto it = std::find(selectedBodies_.begin(), selectedBodies_.end(), hit);
        if (it != selectedBodies_.end()) selectedBodies_.erase(it);
        else selectedBodies_.push_back(hit);
    } else {
        selectedBodies_.assign(1, hit);
    }
    activeBody_ = hit;
}

void Editor::enterPlayMode(PhysicsWorld& world) {
    editSnapshot_ = world.bodies;
    snapshotGravity_ = world.gravityAxis;
    playMode_ = true;
    playing_ = true;
    playStarted_ = true;
    selectedBodies_.clear();
    world.wakeAll();
    Log::info(text("log.play"), (int)editSnapshot_.size());
}

void Editor::exitPlayMode(PhysicsWorld& world) {
    world.bodies = editSnapshot_;
    world.setGravity(snapshotGravity_);
    playMode_ = false;
    playing_ = false;
    timeResetRequested_ = true;
    dragAxis_ = -1;
    Log::info(text("log.stop"));
}

void Editor::saveScene(const PhysicsWorld& world) {
    if (scenes::save(world, meshes_, scenePath())) {
        sceneDirty_ = false;
        Log::info(text("log.saved"), (int)world.bodies.size());
    }
}

void Editor::loadScene(PhysicsWorld& world) {
    if (!scenes::load(world, meshes_, scenePath()))
        return;
    activeBody_ = -1;
    selectedBodies_.clear();
    sceneDirty_ = false;
    Log::info(text("log.loaded"), (int)world.bodies.size());
}

void Editor::renderOverlay(SceneRenderer& scene, PhysicsWorld& world) {
    for (int index : selectedBodies_) {
        const RigidBody* body = world.find(index);
        if (body) scene.renderSelection(*body);
    }
    const RigidBody* body = world.find(activeBody_);
    if (!body) return;

    // Only the tool that is actually active draws handles: Select and Pan show
    // none, and a gizmo left over from the previous tool would be a lie about
    // what a drag would do.
    gizmo::Mode mode = gizmo::Mode::Move;
    if (!gizmoModeFor(tool_, mode)) return;

    Vec3 axes[3];
    gizmo::axesFor(*body, mode, axes);

    Vec3 ringU[3], ringV[3];
    for (int i = 0; i < 3; ++i) gizmo::ringFrame(axes[i], ringU[i], ringV[i]);

    SceneRenderer::GizmoShape shape = SceneRenderer::GizmoShape::Arrows;
    if (mode == gizmo::Mode::Rotate) shape = SceneRenderer::GizmoShape::Rings;
    else if (mode == gizmo::Mode::Scale) shape = SceneRenderer::GizmoShape::Cubes;

    scene.renderGizmo(body->position, axes, gizmo::handleLength(*body),
                      gizmo::ringRadius(*body), ringU, ringV, dragAxis_, shape);
}

void Editor::renderUi(UiRenderer& ui, PhysicsWorld& world, Camera& camera, Window& window,
                      int fbWidth, int fbHeight) {
    ui.begin(fbWidth, fbHeight);

    drawTopBar(ui, world, camera, window, fbWidth);
    drawSceneHeader(ui, world, camera, window);
    drawViewportTools(ui, camera);
    drawDockedPanels(ui, world, window);
    // Floating panels go over everything that is docked: that is what floating
    // means, and the drag that put one there is still in progress underneath.
    drawFloatingPanels(ui, world, window);
    // Overlays last, and after a flush: the dropdowns hang over the panels below
    // the top bar, and the batcher emits every rectangle before any text, so
    // without the flush their background would land under their neighbours' labels.
    ui.flush();
    drawTopBarOverlay(ui, world, camera, window, fbWidth, fbHeight);
    // A dialog is modal over everything, including the dropdowns: it is the only
    // way to be sure the click that opened it cannot also land on the editor.
    drawModal(ui, world, window, fbWidth, fbHeight);
    // Above even the dialogs: it belongs to the pointer, not to the layout.
    drawPanelDragFeedback(ui);

    ui.end();
    pruneSelection(world);
}

// The docked panels, in the order the eye meets them: the tree on the left, the
// inspector on the right, the console under both. A panel that is floating is not
// drawn here - it is drawn again over the scene, which is the only thing that
// makes floating look different from docking.
void Editor::drawDockedPanels(UiRenderer& ui, PhysicsWorld& world, Window& window) {
    for (int i = 0; i < kPanelCount; ++i) {
        if (!panels_[i].open || panels_[i].zone == PanelZone::Float) continue;
        drawPanelById(ui, world, window, (PanelId)i);
    }
}

void Editor::drawFloatingPanels(UiRenderer& ui, PhysicsWorld& world, Window& window) {
    for (int i = 0; i < kPanelCount; ++i) {
        if (panels_[i].zone != PanelZone::Float || !panels_[i].open) continue;
        // While it is being dragged the ghost is drawn instead, so the panel does
        // not run ahead of the pointer.
        if (dragPanel_ == i) continue;
        drawPanelById(ui, world, window, (PanelId)i);
    }
}

void Editor::drawPanelById(UiRenderer& ui, PhysicsWorld& world, Window& window, PanelId id) {
    const Rect panel = panelRect(id);
    ui.fillRect(panel.x, panel.y, panel.width, panel.height, theme::panel);
    switch (id) {
    case PanelId::Hierarchy: drawHierarchyDock(ui, world, window, panel); break;
    case PanelId::Inspector: drawInspectorDock(ui, world, window, panel); break;
    case PanelId::Console: drawConsolePanel(ui, panel); break;
    case PanelId::PanelCount: break;
    }
    drawPanelChrome(ui, id);
}

} // namespace s3d


