#include "editor/Editor.hpp"

#include "core/Log.hpp"
#include "core/Str.hpp"
#include "scene/SceneSerializer.hpp"
#include "ui/Theme.hpp"
#include "editor/Picking.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace s3d {
namespace {

// Console height under the viewport and the inspector when it is docked there.
constexpr float kConsoleHeight = 156.0f;
// Height of a panel's title bar: the handle it is dragged by. Every panel uses the
// same one, so a grip always sits in the same place on all of them.
constexpr float kPanelHeaderHeight = 24.0f;

// Fallback when no project is open: next to the binary.
const char* const kDefaultSceneFile = "scenes/scene.json";

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

// Which way is down, for the number keys: 1 and 2 are up and down, and then
// the four sides, so that the keys go round the way the arrows on a keypad do.
const GravityAxis kGravityAxes[6] = {GravityAxis::NegY, GravityAxis::PosY, GravityAxis::NegX,
                                      GravityAxis::PosX, GravityAxis::NegZ, GravityAxis::PosZ};

} // namespace

// Whether two bodies are the same body in the same state, as far as the
// editor is concerned.
//
// The inspector edits a body in place, so the only way to know that a number
// was changed is to compare the body before and after. The fields that the
// simulation writes every frame - the velocity, the sleep timer - are not
// among them: they change on their own, and a change the user did not make is
// not a change that belongs on the undo stack.
bool sameBodyState(const RigidBody& a, const RigidBody& b) {
    return a.position.x == b.position.x && a.position.y == b.position.y &&
           a.position.z == b.position.z && a.orientation.w == b.orientation.w &&
           a.orientation.x == b.orientation.x && a.orientation.y == b.orientation.y &&
           a.orientation.z == b.orientation.z && a.halfExtents.x == b.halfExtents.x &&
           a.halfExtents.y == b.halfExtents.y && a.halfExtents.z == b.halfExtents.z &&
           a.cylinderRadius == b.cylinderRadius && a.capsuleRadius == b.capsuleRadius &&
           a.capsuleHeight == b.capsuleHeight && a.coneRadius == b.coneRadius &&
           a.coneHeight == b.coneHeight && a.pyramidBaseRadius == b.pyramidBaseRadius &&
           a.pyramidHeight == b.pyramidHeight && a.prismRadius == b.prismRadius &&
           a.prismHeight == b.prismHeight && a.prismSides == b.prismSides &&
           a.cameraFov == b.cameraFov && a.mesh == b.mesh && a.meshScale == b.meshScale &&
           a.mass == b.mass && a.visible == b.visible && a.locked == b.locked &&
           a.favorite == b.favorite && a.activeCamera == b.activeCamera &&
           a.sound == b.sound && a.soundVolume == b.soundVolume &&
           a.anchorPosition == b.anchorPosition && a.anchorRotation == b.anchorRotation &&
           a.anchorPoint.x == b.anchorPoint.x && a.anchorPoint.y == b.anchorPoint.y &&
           a.anchorPoint.z == b.anchorPoint.z && a.anchorSpring == b.anchorSpring &&
           a.material.density == b.material.density &&
           a.material.friction == b.material.friction &&
           a.material.restitution == b.material.restitution &&
           a.material.isStatic == b.material.isStatic &&
           a.material.color.x == b.material.color.x && a.material.color.y == b.material.color.y &&
           a.material.color.z == b.material.color.z;
}

Editor::Editor(MeshRegistry& meshes, i18n::Localizer& localizer, SoundDevice& sound)
    : meshes_(meshes), loc_(localizer), sound_(sound) {
    filter_.id = kFilterField;
    toolbarSearch_.id = kToolbarSearch;
    // The first ground is a little different from the ones after it: a number
    // is drawn for the variation of the ground that is next, and the panel
    // draws a new one every time a ground is made.
    landformSettings_.variation = landformVariation();
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
    // Favorites starts closed and floating: a fourth panel that appears over the
    // scene on the first launch is a panel nobody asked for, and every edge is
    // already taken by a panel that was there before it. The View menu opens it.
    // The two panels that have a place of their own. The rest of the screen is
    // the hierarchy on the left, the inspector on the right and the console
    // along the bottom; what is left of the bottom row are the two corners, and
    // a panel that has no place of its own is a panel nobody opens.
    //
    // Unity puts its Project window in the bottom-left corner for the same
    // reason: the space under the hierarchy is worth more than a gap.
    Panel& favorites = panels_[(int)PanelId::Favorites];
    favorites.zone = PanelZone::BottomLeft;
    favorites.open = true;
    favorites.floatRect = Rect{340.0f, 130.0f, 260.0f, 300.0f};

    Panel& sounds = panels_[(int)PanelId::Sounds];
    sounds.zone = PanelZone::BottomRight;
    sounds.open = true;
    sounds.floatRect = Rect{620.0f, 130.0f, 260.0f, 260.0f};

    // The landform is a tool window: it makes a piece of ground and is in the
    // way of nothing. It starts closed, so a window that opens the editor is
    // not a window with a tool in it; the View menu brings it back.
    Panel& landform = panels_[(int)PanelId::Landform];
    landform.zone = PanelZone::Float;
    landform.open = false;
    landform.floatRect = Rect{600.0f, 60.0f, 360.0f, 430.0f};
}

bool Editor::consumeSingleStep() {
    const bool requested = singleStepRequested_;
    singleStepRequested_ = false;
    return requested;
}

// The window as numbers: the sizes the user chose, where every panel is and
// whether it is open. The geometry is computed from this and nothing else, so
// a test can build one and ask the same questions the editor does.
Workspace Editor::workspace() const {
    Workspace ws;
    ws.fbWidth = fbWidth_;
    ws.fbHeight = fbHeight_;
    ws.leftWidth = leftWidth_;
    ws.rightWidth = rightWidth_;
    ws.bottomHeight = bottomHeight_;
    ws.bottomLeftHeight = bottomLeftHeight_;
    ws.bottomRightHeight = bottomRightHeight_;
    for (int i = 0; i < kPanelCount; ++i) {
        ws.zones[i] = panels_[i].zone;
        ws.open[i] = panels_[i].open;
        ws.floatRects[i] = panels_[i].floatRect;
    }
    ws.dragPanel = dragPanel_;
    ws.dropZone = dropZone_;
    return ws;
}

// Where a panel in `zone` sits this frame. The geometry itself is in
// PanelLayout.hpp, and this is the editor's copy of the state it is computed
// from.
Rect Editor::zoneRect(PanelZone zone) const { return s3d::zoneRect(workspace(), zone); }

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
    case PanelId::Favorites: return text("tab.favorites");
    case PanelId::Sounds: return text("tab.sounds");
    case PanelId::Landform: return text("tab.landform");
    case PanelId::PanelCount: break;
    }
    return "";
}

// Which zone the pointer is over. The bottom row is tested first and split at the
// column edges, so the two bottom corners are their own zones; above it, the
// vertical edges claim the sides and the middle of the scene means "float": dropping
// a panel there is the only way to get one off the edges, exactly as dragging a
// dock out of a Unity window does.
//
// The column edges come from the dock widths themselves rather than from the same
// thresholds as the vertical edges, because a panel dropped in the bottom-left
// corner has to land exactly where zoneRect() will draw it - a preview that is not
// the result is a drop the user has to undo.
PanelZone Editor::zoneAt(float x, float y) const {
    const float width = (float)fbWidth_;
    const float height = (float)fbHeight_;
    const float strip = std::fmin(200.0f, std::fmax(120.0f, height * 0.28f));
    if (y >= height - strip) {
        if (x < leftWidth_) return PanelZone::BottomLeft;
        if (x > width - rightWidth_) return PanelZone::BottomRight;
        return PanelZone::Bottom;
    }

    const float edge = std::fmin(200.0f, width * 0.25f);
    if (x <= edge) return PanelZone::Left;
    if (x >= width - edge) return PanelZone::Right;
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
        // And not on top of another floating panel: a window that covers
        // another one hides it, and the one underneath cannot be reached.
        // The window is moved to the first place where it covers nothing -
        // which is not always the place it was dropped, and that is the point.
        rect = findFreeSpot(workspace(), dragPanel_, rect);
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
    markLayoutDirty();
}

// Which grips of a panel the pointer is on, as a set of edge bits, or 0 for none.
//
// A docked panel has exactly one: the edge that faces the scene. A floating one
// has all four, and their corners take two edges at once.
//
// The geometry of it is in PanelLayout.hpp, where a test can ask the same
// question without a window. This is the editor's version of it, with the one
// thing the geometry does not know about: the X in the title bar wins its
// corner. It sits in the top right, which is exactly where a dock's grip or a
// floating panel's corner is, and a close button that resized the panel instead
// of closing it would be the most annoying thing in the window.
unsigned Editor::panelGripsAt(int index, float x, float y) const {
    if (index < 0 || index >= kPanelCount || !panels_[index].open) return 0;
    if (panelCloseRect((PanelId)index).contains(x, y)) return 0;
    return gripsAt(workspace(), index, x, y);
}

// A floating panel follows the pointer from the rectangle it had when the grip was
// taken. The edges the pointer does not hold stay where they were, which is the
// whole difference between resizing a window and stretching it.
void Editor::applyFloatingResize(float x, float y) {
    const Rect start = resizeStartRect_;
    const float dx = x - resizeStartPointerX_;
    const float dy = y - resizeStartPointerY_;
    const bool left = (resizeEdges_ & kGripLeft) != 0;
    const bool right = (resizeEdges_ & kGripRight) != 0;
    const bool top = (resizeEdges_ & kGripTop) != 0;
    const bool bottom = (resizeEdges_ & kGripBottom) != 0;

    Rect rect = start;
    if (left) rect.x = start.x + dx;
    if (right) rect.width = start.width + dx;
    if (top) rect.y = start.y + dy;
    if (bottom) rect.height = start.height + dy;

    // Clamped against the window and the minimum. The two held edges are then
    // re-derived from the final size: otherwise hitting the minimum makes the
    // edge the pointer is NOT holding jump, which is the one thing a window must
    // never do.
    rect.width = clampf(rect.width, std::fmin(kPanelMinWidth, (float)fbWidth_),
                        std::fmax(kPanelMinWidth, (float)fbWidth_));
    rect.height = clampf(rect.height, std::fmin(kPanelMinHeight, (float)fbHeight_),
                         std::fmax(kPanelMinHeight, (float)fbHeight_));
    if (left) rect.x = start.x + start.width - rect.width;
    if (top) rect.y = start.y + start.height - rect.height;
    // Same rule a dropped panel follows: a floating window cannot leave the one it
    // floats over, or it becomes a window the user has to hunt for.
    rect.x = clampf(rect.x, 0.0f, std::fmax(0.0f, (float)fbWidth_ - rect.width));
    rect.y = clampf(rect.y, (float)theme::menuBarHeight,
                    std::fmax(0.0f, (float)fbHeight_ - rect.height));

    panels_[resizePanel_].floatRect = rect;
}

void Editor::handlePanelDrag(PhysicsWorld& world) {
    const float px = ui_.pointerX();
    const float py = ui_.pointerY();

    // --- resizing, the other half of "move the panels" --------------------
    if (resizePanel_ >= 0) {
        if (!ui_.pointerDown()) {
            // The drag ended. Whatever it left behind is the size the workspace
            // is saved with.
            resizePanel_ = -1;
            resizeEdges_ = 0;
            markLayoutDirty();
            return;
        }
        if (panels_[resizePanel_].zone == PanelZone::Float) {
            applyFloatingResize(px, py);
            return;
        }
        const PanelZone zone = panels_[resizePanel_].zone;
        // What the drag changes is decided by the GRIP, not by the zone the
        // panel is in: the panel in the bottom-left corner has two grips - its
        // top edge, which makes it taller, and its right edge, which is the
        // edge of the column it shares with the hierarchy. Taking the zone for
        // the answer made both of them change the height, and dragging the
        // column's edge moved a panel that was not in the way of it.
        //
        // The arithmetic is in PanelLayout.hpp, where a test can ask the same
        // question: a grip that moved the wrong size was found by looking at
        // the screen, which is how the bugs of this kind used to be found.
        const float dx = px - resizeStartPointerX_;
        const float dy = py - resizeStartPointerY_;
        const WorkspaceSizes sizes =
            applyResize(workspace(), resizePanel_, resizeEdges_, resizeStartSize_, dx, dy);
        leftWidth_ = sizes.leftWidth;
        rightWidth_ = sizes.rightWidth;
        bottomHeight_ = sizes.bottomHeight;
        bottomLeftHeight_ = sizes.bottomLeftHeight;
        bottomRightHeight_ = sizes.bottomRightHeight;
        (void)zone;
        return;
    }

    if (ui_.pointerPressed() && pointerArmed_) {
        // A press on a window that is over this one is a press on that window.
        // The floating panels and the dialog are in the guard, and a press that
        // belongs to them is not a press on the panel underneath.
        if (ui_.pressOnOverlay()) return;

        // Grips are tested BEFORE the title bar, and that order is the whole
        // reason the console can be resized at all: its only grip is its top
        // edge, which is also the first five pixels of its title bar. Tested the
        // other way round, a press on the console's border picked the panel up
        // and dragged it away instead of making it taller.
        for (int i = 0; i < kPanelCount; ++i) {
            const unsigned grips = panelGripsAt(i, px, py);
            if (!grips) continue;
            resizePanel_ = i;
            resizeEdges_ = grips;
            resizeStartRect_ = panels_[i].floatRect;
            resizeStartPointerX_ = px;
            resizeStartPointerY_ = py;
            const PanelZone zone = panels_[i].zone;
            // The number the drag starts from is the one the grip moves: the
            // height of a panel in the bottom row, or the width of the column
            // it shares with the panel above it.
            if (resizeEdges_ & kGripTop) {
                resizeStartSize_ = zone == PanelZone::BottomLeft ? bottomLeftHeight_
                                   : zone == PanelZone::BottomRight ? bottomRightHeight_
                                                                   : bottomHeight_;
            } else {
                resizeStartSize_ = zoneIsLeft(zone) ? leftWidth_ : rightWidth_;
            }
            return;
        }
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

    if (dragPanel_ < 0) return;

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
    const Rect rect = panelRect(id);
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
    //
    // The X is two strokes, and a stroke is a line of squares that touch: the
    // step between the squares is their own size, so the diagonals come out as
    // two lines and not as a scatter of dots that only look like an X when you
    // know what it is.
    const Rect close = panelCloseRect(id);
    const bool closeHovered = ui_.hovered(close);
    const Color tint = closeHovered ? theme::error : theme::textDim;
    constexpr float kStroke = 2.4f;
    for (int i = 0; i < 5; ++i) {
        const float o = 3.0f + (float)i * kStroke;
        ui.fillRect(close.x + o, close.y + o, kStroke, kStroke, tint);
        ui.fillRect(close.x + 12.0f - o + 2.0f, close.y + o, kStroke, kStroke, tint);
    }
    // The X is not a widget but a shape drawn by hand, so it does not go
    // through the widget layer that decides who owns the press. It asks the
    // layer instead: a widget under the pointer has already taken the press,
    // and two things answering the same click is how a button under a button
    // gets pressed.
    if (pressAccepted() && closeHovered && !ui_.pressOnOverlay() &&
        !ui_.pressClaimedByWidget()) {
        panels_[(int)id].open = false;
        markLayoutDirty();
        return;
    }

    if (floating) {
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
        // The resize corner: a line of squares up the diagonal, the mark every
        // resizable window carries. A floating panel that can be resized but
        // shows no sign of it is a panel nobody resizes - and a mark made of
        // squares that do not touch is not a line but a scatter of dots.
        const Color corner = ui_.hovered(rect) ? theme::textDim : theme::textDim.scaled(0.55f);
        constexpr float kStep = 2.2f;
        for (int i = 0; i < 4; ++i)
            ui.fillRect(rect.x + rect.width - 5.0f - (float)i * kStep,
                        rect.y + rect.height - 5.0f - (float)i * kStep, kStep, kStep, corner);
        return;
    }

    // The divider between a docked panel and the scene. It is the panel's resize
    // grip, so it is drawn whether or not anybody is touching it: an edge the user
    // cannot see is an edge they never try to drag. Two edges get one - a panel in
    // a corner sits between two pieces of the scene and needs both to read as
    // docked rather than as a stray window.
    if (zoneIsBottom(panel.zone))
        ui.fillRect(rect.x, rect.y, rect.width, 1.0f, theme::separator);
    if (zoneIsLeft(panel.zone))
        ui.fillRect(rect.x + rect.width - 1.0f, rect.y, 1.0f, rect.height, theme::separator);
    if (zoneIsRight(panel.zone))
        ui.fillRect(rect.x, rect.y, 1.0f, rect.height, theme::separator);
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
    // The resize handles. Every open panel is asked where its grips are: the ones
    // under the pointer light up before it is pressed, and the ones being dragged
    // stay lit for as long as the drag lasts. Drawn from the same hit test the
    // drag uses, so the edge that lights up is the edge that takes the pointer.
    for (int i = 0; i < kPanelCount; ++i) {
        if (!panels_[i].open) continue;
        const bool active = resizePanel_ == i;
        const unsigned grips =
            active ? resizeEdges_
                   : panelGripsAt(i, ui_.pointerX(), ui_.pointerY());
        if (!grips) continue;

        const Rect rect = panelRect((PanelId)i);
        const bool floating = panels_[i].zone == PanelZone::Float;
        // Accent on both, brighter while the drag lasts: a divider nobody can see
        // is a divider nobody finds, and the whole point of this handle is that it
        // is found without reading anything.
        const Color tint = theme::accent;
        // The handle is a line, and a line has no gaps in it: one rectangle along
        // the whole edge, at a whole number of pixels, so the edge of the panel
        // is lit from end to end and not in pieces.
        const float thickness = active ? 4.0f : 3.0f;
        const float offset = (thickness - 1.0f) * 0.5f;
        if (grips & kGripLeft)
            ui.fillRect(rect.x - offset, rect.y, thickness, rect.height, tint);
        if (grips & kGripRight)
            ui.fillRect(rect.x + rect.width - thickness + offset, rect.y, thickness, rect.height,
                        tint);
        if (grips & kGripTop)
            ui.fillRect(rect.x, rect.y - offset, rect.width, thickness, tint);
        if (grips & kGripBottom)
            ui.fillRect(rect.x, rect.y + rect.height - thickness + offset, rect.width, thickness,
                        tint);
        // A corner the pointer is really on gets a block, so grabbing two edges
        // at once is visible before the drag and not only after it.
        if (floating && !active) {
            const float size = 8.0f;
            if ((grips & kGripLeft) && (grips & kGripTop))
                ui.fillRect(rect.x - 2.0f, rect.y - 2.0f, size, size, tint);
            if ((grips & kGripRight) && (grips & kGripTop))
                ui.fillRect(rect.x + rect.width - size + 2.0f, rect.y - 2.0f, size, size, tint);
            if ((grips & kGripLeft) && (grips & kGripBottom))
                ui.fillRect(rect.x - 2.0f, rect.y + rect.height - size + 2.0f, size, size, tint);
            if ((grips & kGripRight) && (grips & kGripBottom))
                ui.fillRect(rect.x + rect.width - size + 2.0f,
                            rect.y + rect.height - size + 2.0f, size, size, tint);
        }
    }
}

void Editor::recomputeLayout(int fbWidth, int fbHeight) {
    fbWidth_ = fbWidth;
    fbHeight_ = fbHeight;

    // The geometry is in PanelLayout.hpp, and this is the editor's copy of the
    // state it is computed from.
    Workspace ws = workspace();
    ws.fbWidth = fbWidth;
    ws.fbHeight = fbHeight;
    layout_ = computeLayout(ws);
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
    // Whatever was being changed before is finished first: one change is one
    // entry on the undo stack, and the steps of one entry all talk about the
    // same scene - a body that was removed and a body that was moved in the
    // same entry would not know which is which.
    commitUndo();
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
        // Remembered before it goes, so that Ctrl+Z can bring it back.
        rememberRemoval(world, index);
        world.remove(index);
        markSceneDirty();
    }
    commitUndo();
    selectedBodies_.clear();
    activeBody_ = -1;
}

void Editor::updateViewRay(const Camera& camera) {
    if (viewRay_.valid) return;
    camera.basis(viewRay_.forward, viewRay_.right, viewRay_.up);

    // The ray is built for the viewport, because that is the space the scene is
    // drawn in and the space the projection works in. The camera's field of view
    // is over the viewport's height, and the horizontal one follows from its
    // aspect: the same two numbers the projection matrix is built from, so the
    // pointer and the picture cannot disagree about where a thing is.
    const Rect view = layout_.viewport;
    picking::viewRayTangents(camera, view.width, view.height, viewRay_.tanX, viewRay_.tanY);
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

void Editor::wrapText(const UiRenderer& ui, float maxWidth, const char* utf8,
                      std::vector<std::string>& out) {
    out.clear();
    if (!utf8 || !*utf8 || maxWidth <= 0.0f) return;

    // Word by word: a line that does not fit starts a new one. A word longer
    // than the box is left whole - the caller draws it smaller, and a word cut
    // in half is a word nobody can read.
    std::string line;
    size_t at = 0;
    const std::string_view all(utf8);
    while (at <= all.size()) {
        const size_t space = all.find(' ', at);
        const std::string_view word =
            all.substr(at, space == std::string_view::npos ? std::string_view::npos : space - at);
        const std::string candidate =
            line.empty() ? std::string(word) : line + " " + std::string(word);
        if (!line.empty() && ui.textWidth(candidate.c_str()) > maxWidth) {
            out.push_back(line);
            line = std::string(word);
        } else {
            line = candidate;
        }
        if (space == std::string_view::npos) break;
        at = space + 1;
    }
    if (!line.empty()) out.push_back(line);
}

void Editor::handleInput(PhysicsWorld& world, Camera& camera, Window& window, int fbWidth,
                         int fbHeight, float dt) {
    ui_.beginInput(window);
    // The dropdowns are drawn after the panels, so the panels' hit tests have already
    // run by the time anything can see that they are covered. Claiming the pointer
    // here - before the panel drag and the viewport pick, which both read the same
    // press snapshot - is what stops a click on a menu row from also dragging a dock
    // or selecting a body in the scene underneath it.
    ui_.claimPointer(popupOpen() ? kPopupCapture : 0);
    viewRay_.valid = false;

    // The window changed size. The pointer's position in the framebuffer moved
    // with it, and a drag that went on would take that movement for the user's
    // own and move a panel by it - the panel ends up where the window's edge
    // was, not where the hand is. The drag is cancelled instead: the hand did
    // not do it, so the hand is not asked to finish it.
    if (fbWidth != fbWidth_ || fbHeight != fbHeight_) {
        dragPanel_ = -1;
        resizePanel_ = -1;
        resizeEdges_ = 0;
    }

    // A press counts only after a release. The button can already be down when
    // the editor appears - the click that opened the project is the launcher's
    // and the mouse is still held - and a press here would be the second half
    // of somebody else's click: the one that picked a panel up and dragged it
    // to the end of its track, and then wrote the workspace out with the panel
    // at its smallest.
    if (!pointerArmed_ && !ui_.pointerDown()) pointerArmed_ = true;

    recomputeLayout(fbWidth, fbHeight);
    // The workspace comes back the way it was left, but only once the window size
    // is known: a floating panel restored onto a screen that has since shrunk has
    // to be brought back into it. Saving waits for the next frame, so what is
    // written is a layout the user has already seen.
    if (!layoutLoaded_) {
        layoutLoaded_ = true;
        loadLayout();
    }
    if (layoutDirty_) {
        layoutDirty_ = false;
        saveLayout();
    }
    // The floating panels and the dialog are surfaces: they are over the scene
    // and over the panels below them, and a press on one of them belongs to
    // it. The guard is what says so, and it has to be filled before the
    // viewport is picked - a click on a window that is over the scene must
    // not also grab the body behind it.
    declareFloatingPanels();
    if (modal_ != Modal::None)
        ui_.overlays().push(0.0f, 0.0f, (float)fbWidth, (float)fbHeight, kOwnerModal);
    handleHotkeys(world, camera, window, dt);
    handlePanelDrag(world);
    handleViewport(world, camera, window);
}

void Editor::handleHotkeys(PhysicsWorld& world, Camera& camera, Window& window, float dt) {
    // A field with the keyboard has it. The shortcuts belong to the editor
    // and not to the person who is typing a name into the search box: a
    // Delete that throws the selection away while somebody is typing is not
    // a shortcut, it is a mistake - and the typed name goes with it.
    if (ui_.textFocused()) return;
    // A dialog is modal: it has the keyboard until it is answered.
    if (modal_ != Modal::None) return;

    // Ctrl+Z takes the last change back.
    if (ui_.ctrlHeld() && window.takeKeyPress(GLFW_KEY_Z)) {
        undo(world);
        return;
    }

    // The number keys pick which way is down.
    //
    // They are here and not in the shell around the editor because a field
    // with the keyboard has it: a digit typed into a search box is a digit
    // typed into a search box and not a new gravity, and a world that turns
    // on its side because somebody typed a "3" is a thing nobody can explain.
    //
    // And it is an edit to the document: the gravity is saved with the scene.
    for (int i = 0; i < 6; ++i) {
        if (!window.takeKeyPress(GLFW_KEY_1 + i)) continue;
        world.setGravity(kGravityAxes[i]);
        markSceneDirty();
        return;
    }

    // Delete throws the selection away. The menu has done this for as long as
    // there has been a menu, and the key is the one everybody reaches for.
    if (window.takeKeyPress(GLFW_KEY_DELETE)) {
        if (!selectedBodies_.empty()) deleteSelection(world);
        return;
    }

    // The arrows move the camera, in the plane of the screen: left and right
    // along the camera's right, up and down along its up. The camera does not
    // turn - the mouse does that - so the scene slides past instead, and what
    // is being looked at stays in the middle of the screen.
    Vec3 forward, right, up;
    camera.basis(forward, right, up);
    Vec3 move;
    if (window.keyDown(GLFW_KEY_LEFT)) move -= right;
    if (window.keyDown(GLFW_KEY_RIGHT)) move += right;
    if (window.keyDown(GLFW_KEY_UP)) move += up;
    if (window.keyDown(GLFW_KEY_DOWN)) move -= up;
    if (move.lengthSq() <= 0.0f) return;

    // How fast the ground slides past: a piece of it you can see. The same
    // rule the pan tool uses - the speed follows how far away the ground is,
    // so a key moves the same amount of the picture at any distance.
    Vec3 reference{0.0f, 0.0f, 0.0f};
    if (const RigidBody* body = world.find(activeBody_)) reference = body->position;
    const float distance = std::fmax(1.0f, (camera.position - reference).length());
    float speed = clampf(distance * 0.35f, 3.0f, 120.0f);
    if (window.keyDown(GLFW_KEY_LEFT_SHIFT) || window.keyDown(GLFW_KEY_RIGHT_SHIFT)) speed *= 4.0f;
    camera.position += move.normalized() * (speed * dt);
    viewRay_.valid = false;
}

// --- the undo ----------------------------------------------------------------

// Remembers a body as it is now, so that a change to it can be taken back.
void Editor::rememberBody(const PhysicsWorld& world, int index) {
    if (undoing_) return;
    if (index < 0 || index >= (int)world.bodies.size()) return;
    UndoStep step;
    step.kind = UndoStep::Kind::Changed;
    step.index = index;
    step.body = world.bodies[(size_t)index];
    pending_.steps.push_back(step);
}

// Remembers a body that is about to be removed.
void Editor::rememberRemoval(const PhysicsWorld& world, int index) {
    if (undoing_) return;
    if (index < 0 || index >= (int)world.bodies.size()) return;
    UndoStep step;
    step.kind = UndoStep::Kind::Removed;
    step.index = index;
    step.body = world.bodies[(size_t)index];
    pending_.steps.push_back(step);
}

// Remembers a body that has just been added.
void Editor::rememberAddition(const PhysicsWorld& world, int index) {
    if (undoing_) return;
    if (index < 0 || index >= (int)world.bodies.size()) return;
    UndoStep step;
    step.kind = UndoStep::Kind::Added;
    step.index = index;
    step.body = world.bodies[(size_t)index];
    pending_.steps.push_back(step);
}

// Puts the change that was being made onto the undo stack.
void Editor::commitUndo() {
    if (undoing_) return;
    if (!pending_.steps.empty()) {
        undoStack_.push_back(std::move(pending_));
        // The stack is not endless: a hundred changes is more than anybody
        // undoes, and every entry is a copy of a body.
        if ((int)undoStack_.size() > kMaxUndoActions) undoStack_.erase(undoStack_.begin());
        // A new change clears the way back: the redo stack belongs to a
        // history that has just been broken.
        redoStack_.clear();
    }
    pending_.steps.clear();
    bodyBeforeValid_ = false;
}

// A change to the body that is selected, made by the inspector.
//
// The state to go back to is the one from before the change began, not the
// one from the frame before: a slider that is dragged is one change and not
// one change per pixel.
void Editor::noteBodyEdited(const PhysicsWorld& world, int index) {
    if (undoing_) return;
    if (index < 0 || index >= (int)world.bodies.size()) return;
    if (bodyBeforeValid_ && bodyBeforeIndex_ == index) return;

    if (!bodySnapshotValid_ || bodySnapshotIndex_ != index) return;

    bodyBefore_ = bodySnapshot_;
    bodyBeforeIndex_ = index;
    bodyBeforeValid_ = true;
    pending_.steps.clear();
    UndoStep step;
    step.kind = UndoStep::Kind::Changed;
    step.index = index;
    step.body = bodyBefore_;
    pending_.steps.push_back(step);
}

// Ends the change that was being made, if it is over.
//
// A change is over when the body stops changing: the mouse was let go of,
// the number was typed and Enter was pressed. Until then it is one entry on
// the undo stack and not one entry per frame.
//
// The comparison is with the body as it was at the end of the LAST frame -
// not with the body as it was before the change began. A drag of a slider
// leaves the body different from where it started, and it is the difference
// between one frame and the next that says whether anything is still moving.
void Editor::finishUndoIfDone(const PhysicsWorld& world) {
    if (!bodyBeforeValid_) return;
    if (bodyBeforeIndex_ < 0 || bodyBeforeIndex_ >= (int)world.bodies.size()) {
        bodyBeforeValid_ = false;
        return;
    }
    if (!bodySnapshotValid_ || bodySnapshotIndex_ != bodyBeforeIndex_) {
        // There is nothing to compare with: the change is over.
        commitUndo();
        return;
    }
    const RigidBody& now = world.bodies[(size_t)bodyBeforeIndex_];
    if (sameBodyState(now, bodySnapshot_)) commitUndo();
}

// Takes the last change back, and puts it on the redo stack.
void Editor::undo(PhysicsWorld& world) {
    if (undoStack_.empty()) return;
    // A change that is still being made is finished first: the thing that is
    // being changed right now is the newest thing, and the one before it is
    // the one that is undone.
    if (!pending_.steps.empty()) commitUndo();
    if (undoStack_.empty()) return;

    UndoAction action = std::move(undoStack_.back());
    undoStack_.pop_back();

    undoing_ = true;
    // The steps are taken back in the reverse of the order they were made in,
    // and every one of them knows the index it was made at - so a body that
    // was removed goes back to the place it came from.
    int restored = -1;
    for (int i = (int)action.steps.size() - 1; i >= 0; --i) {
        const UndoStep& step = action.steps[(size_t)i];
        switch (step.kind) {
        case UndoStep::Kind::Changed:
            if (step.index < 0 || step.index >= (int)world.bodies.size()) break;
            world.bodies[(size_t)step.index] = step.body;
            world.bodies[(size_t)step.index].configure(world.bodies[(size_t)step.index].mass);
            world.bodies[(size_t)step.index].syncPrevTransform();
            break;
        case UndoStep::Kind::Added:
            if (step.index < 0 || step.index > (int)world.bodies.size()) break;
            world.bodies.erase(world.bodies.begin() + step.index);
            break;
        case UndoStep::Kind::Removed:
            if (step.index < 0 || step.index > (int)world.bodies.size()) break;
            world.bodies.insert(world.bodies.begin() + step.index, step.body);
            if (restored < 0) restored = step.index;
            break;
        }
    }
    undoing_ = false;

    redoStack_.push_back(std::move(action));
    // The selection follows the bodies: an index that no longer means the
    // same body is not a selection.
    pruneSelection(world);
    // A body that was taken away and is back again is selected, the way it
    // was before it went: the thing that was just undid is the thing you are
    // looking at.
    if (restored >= 0 && restored < (int)world.bodies.size()) {
        activeBody_ = restored;
        selectedBodies_.assign(1, restored);
    }
    world.wakeAll();
    markSceneDirty();
    Log::info("Undo: %d change(s) taken back", (int)redoStack_.back().steps.size());
}

// Puts the last change back the other way.
void Editor::redo(PhysicsWorld& world) {
    if (redoStack_.empty()) return;
    UndoAction action = std::move(redoStack_.back());
    redoStack_.pop_back();

    undoing_ = true;
    for (int i = (int)action.steps.size() - 1; i >= 0; --i) {
        const UndoStep& step = action.steps[(size_t)i];
        switch (step.kind) {
        case UndoStep::Kind::Changed:
            if (step.index < 0 || step.index >= (int)world.bodies.size()) break;
            world.bodies[(size_t)step.index] = step.body;
            world.bodies[(size_t)step.index].configure(world.bodies[(size_t)step.index].mass);
            world.bodies[(size_t)step.index].syncPrevTransform();
            break;
        case UndoStep::Kind::Added:
            if (step.index < 0 || step.index > (int)world.bodies.size()) break;
            world.bodies.erase(world.bodies.begin() + step.index);
            break;
        case UndoStep::Kind::Removed:
            if (step.index < 0 || step.index > (int)world.bodies.size()) break;
            world.bodies.insert(world.bodies.begin() + step.index, step.body);
            break;
        }
    }
    undoing_ = false;

    undoStack_.push_back(std::move(action));
    pruneSelection(world);
    world.wakeAll();
    markSceneDirty();
}

bool Editor::tryNavigationGizmo(Camera& camera) {
    const Rect viewport = layout_.viewport;
    const float centerX = viewport.x + viewport.width - 52.0f;
    const float centerY = viewport.y + 52.0f;
    if (!pressAccepted()) return false;

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

    // The handles are picked in the viewport, because that is the space the
    // scene is drawn in and the space the projection works in: the handles have
    // to be where the things they belong to are drawn.
    const Rect area = layout_.viewport;
    const Vec3 rayOrigin = camera.position;
    const Vec3 rayDir = viewportRay(camera);
    const float pointerX = ui_.pointerX() - area.x;
    const float pointerY = ui_.pointerY() - area.y;

    dragAxis_ = mode == gizmo::Mode::Rotate
                    ? gizmo::pickRing(pointerX, pointerY, rayOrigin, rayDir, camera.position,
                                      viewRay_.forward, viewRay_.right, viewRay_.up,
                                      viewRay_.tanX, viewRay_.tanY, area, *body)
                    : gizmo::pickAxis(pointerX, pointerY, rayOrigin, rayDir, camera.position,
                                      viewRay_.forward, viewRay_.right, viewRay_.up,
                                      viewRay_.tanX, viewRay_.tanY, area, *body, mode);
    if (dragAxis_ < 0) return false;

    // Whatever was being changed before is finished first, and then the body
    // is remembered as it is: the whole drag is one change and Ctrl+Z can
    // take it back in one go.
    commitUndo();
    rememberBody(world, activeBody_);

    dragOrigin_ = body->position;
    Vec3 axes[3];
    gizmo::axesFor(*body, mode, axes);

    if (mode == gizmo::Mode::Rotate) {
        // Remember the orientation and the axis, and take the pointer's angle as
        // the reference the first steps are measured from. The rings turn about
        // world axes (see gizmo::axesFor), so `axes[dragAxis_]` stays put for the
        // whole drag - which is what makes the steps below add up to a turn and
        // not to a wander.
        dragStartOrientation_ = body->orientation;
        rotateAxis_ = axes[dragAxis_];
        rotateAccumulated_ = 0.0f;
        rotateAngleValid_ =
            ringScreenAngle(body->position, camera, rotateLastScreenAngle_);
        return true;
    }

    float rayT = 0.0f;
    picking::closestOnAxis(rayOrigin, rayDir, dragOrigin_, axes[dragAxis_],
                           dragStartAxisT_, rayT);
    return true;
}

// Where the pointer stands on a ring, in screen space.
//
// Three bugs came out of measuring this in the world instead - the pointer ray
// crossed against the ring's plane and the angle was read from there:
//
//   * atan2 wraps at +/-180 degrees, so a pointer that went round the far side of
//     the ring produced a difference of a full turn and the body span once;
//   * a ray almost parallel to that plane has no answer, and the answer it got
//     was zero - the body jumped to whatever "zero degrees" meant;
//   * near the edge of the ring a pixel of hand movement moved the intersection a
//     long way, and the rotation jumped or stopped instead of following.
//
// Reading the angle on screen has none of those: the centre is a projection, the
// angle is atan2 around it, and each frame contributes the shortest way round.
// What is left - a ring seen exactly edge-on - is a line instead of a circle, and
// that is what every editor does with it.
bool Editor::ringScreenAngle(const Vec3& centre, const Camera& camera, float& angleOut) const {
    // The viewport, for the same reason the handles are picked there: it is the
    // space the projection works in.
    const Rect area = layout_.viewport;
    float cx = 0.0f, cy = 0.0f;
    if (!picking::projectToViewport(centre, camera.position, viewRay_.forward, viewRay_.right,
                                    viewRay_.up, viewRay_.tanX, viewRay_.tanY, area.width,
                                    area.height, cx, cy))
        return false;

    const float px = ui_.pointerX() - area.x;
    const float py = ui_.pointerY() - area.y;
    // Standing on the centre: the angle there is noise, and noise reads as a body
    // that twitches while the hand is perfectly still.
    if (std::fabs(px - cx) < 4.0f && std::fabs(py - cy) < 4.0f) return false;

    // Counter-clockwise positive, hence the flipped y: screen y grows downwards.
    angleOut = std::atan2(cy - py, px - cx);
    return true;
}

void Editor::applyGizmoDrag(PhysicsWorld& world, Camera& camera) {
    RigidBody* body = world.find(activeBody_);
    if (!body) return;

    const Vec3 rayOrigin = camera.position;
    const Vec3 rayDir = viewportRay(camera);

    gizmo::Mode mode = gizmo::Mode::Move;
    if (!gizmoModeFor(tool_, mode)) return;

    Vec3 axes[3];
    gizmo::axesFor(*body, mode, axes);

    if (mode == gizmo::Mode::Rotate) {
        float angle = 0.0f;
        if (!ringScreenAngle(body->position, camera, angle)) return;  // no answer: hold still
        float step = 0.0f;
        if (rotateAngleValid_) {
            // The hand went round the far side of the ring and the difference came
            // out as a whole turn. Folded to the shortest way round, which is what
            // "keep turning" means.
            step = std::remainder(angle - rotateLastScreenAngle_, 2.0f * kPi);
        }
        rotateLastScreenAngle_ = angle;
        rotateAngleValid_ = true;
        if (std::fabs(step) < 1e-5f) return;

        // Which way round depends on which way the axis faces. Looking down the
        // +Z axis from the front, a positive turn is counter-clockwise on screen;
        // looking at it from behind, the same turn is clockwise. The pointer's
        // angle is measured counter-clockwise, so the two cases are one sign flip:
        // an axis pointing away from the camera (into the screen, where the
        // camera-to-object vector agrees with it) turns the other way round.
        if (dot(rotateAxis_, body->position - camera.position) > 0.0f) step = -step;

        rotateAccumulated_ += step;
        body->orientation =
            (Quat::axisAngle(rotateAxis_, rotateAccumulated_) * dragStartOrientation_)
                .normalized();
        body->syncPrevTransform();
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
        // The hand moved the body, not the simulation, so the render interpolation
        // has to be told or the model is drawn from wherever it was before the drag.
        body->syncPrevTransform();
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

void Editor::drawCameraIcon(UiRenderer& ui, float x, float y, float size,
                             const Color& tint) {
    // A triangle, pointing right: the shape a camera is drawn as everywhere
    // else. The batcher has no triangle, so it is built out of rows of
    // rectangles - the same way the toolbar's play button is.
    constexpr int kRows = 5;
    const float rowHeight = size / (float)kRows;
    for (int i = 0; i < kRows; ++i) {
        // The apex is on the right, so the widest row is the first one.
        const float t = 1.0f - (float)i / (float)(kRows - 1);
        const float width = size * t;
        ui.fillRect(x, y + (float)i * rowHeight, width, rowHeight + 0.5f, tint);
    }
}

void Editor::handleViewport(PhysicsWorld& world, Camera& camera, Window& window) {
    if (ui_.cameraCaptured()) {
        dragAxis_ = -1;
        return;
    }

    // A press on a window that is over the scene belongs to the window: a
    // click on a floating panel must not also pick the body behind it, and a
    // dialog has the whole window to itself.
    if (ui_.pressOnOverlay()) {
        if (ui_.pointerReleased()) dragAxis_ = -1;
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
        if (pressAccepted()) {
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

    if (pressAccepted()) {
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
        if (ui_.pointerReleased()) {
            dragAxis_ = -1;
            // The drag is over: the body as it was before it is one entry on the
            // undo stack.
            commitUndo();
        }
        return;
    }
    if (ui_.pointerReleased() && dragAxis_ >= 0) {
        dragAxis_ = -1;
        commitUndo();
        return;
    }
    if (pressAccepted())
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
    // Raised for Application to pick up next frame, not taken here: the editor
    // cannot reach the script host, and the dependency only points up. Set after
    // the snapshot is back in place so a script's onStop sees the edit-mode world.
    playStopped_ = true;
    Log::info(text("log.stop"));
}

void Editor::saveScene(const PhysicsWorld& world) {
    // The scene is JSON, and the file it is in says so. A project that still has
    // the old text file moves to the JSON one the first time it is saved: the
    // text file is left where it is - the reader still understands it - and the
    // project stops using it.
    std::string target = scenePath_;
    if (!str::equalsIgnoreAscii(str::extensionView(target), ".json")) {
        std::string name = str::fileName(target);
        const size_t dot = name.find_last_of('.');
        if (dot != std::string::npos) name = name.substr(0, dot);
        target = str::join(scenesDir(), name + ".json");
    }

    if (!scenes::save(world, meshes_, target)) return;
    if (target != scenePath_) {
        scenePath_ = target;
        // The project has to say which file its scene is in, or the next time it
        // is opened it goes back to the old one and the scene that was saved is
        // gone.
        shellRequest_ = ShellRequest::SaveProject;
    }
    sceneDirty_ = false;
    Log::info(text("log.saved"), (int)world.bodies.size());
}

void Editor::loadScene(PhysicsWorld& world) {
    if (!scenes::load(world, meshes_, scenePath()))
        return;
    activeBody_ = -1;
    selectedBodies_.clear();
    sceneDirty_ = false;
    Log::info(text("log.loaded"), (int)world.bodies.size());
}

void Editor::renderOverlay(SceneRenderer& scene, PhysicsWorld& world, int hiddenBody) {
    for (int index : selectedBodies_) {
        if (index == hiddenBody) continue;
        const RigidBody* body = world.find(index);
        if (body) scene.renderSelection(*body);
    }
    if (activeBody_ == hiddenBody) return;
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

bool Editor::popupOpen() const {
    return openMenu_ >= 0 || layersOpen_ || helpPage_ != 0 || modal_ != Modal::None ||
           !toolbarSearch_.empty();
}

void Editor::renderUi(UiRenderer& ui, PhysicsWorld& world, Camera& camera, Window& window,
                      int fbWidth, int fbHeight) {
    ui.begin(fbWidth, fbHeight);

    // A dialog takes the whole frame, bar included - it is modal, and a "Save" in
    // the top bar while a "Save As?" is on screen is two answers to one click. The
    // bar's own overlays are the opposite case: they hang off the bar, so the bar
    // has to stay live to switch one for another.
    ui_.setPointerCapture(0);
    drawTopBar(ui, world, camera, window, fbWidth);
    // Overlays are registered here, before the panels draw, because the hit test
    // runs in the middle of the frame and what is on top has to be known by then.
    // The guard answers it for every widget below; nothing claims the pointer by
    // hand, which is what used to break whenever a new overlay forgot to.
    declareTopBarOverlays(ui, fbWidth, fbHeight, (int)world.bodies.size());

    drawSceneHeader(ui, world, camera, window);
    drawViewportTools(ui, camera);
    drawDockedPanels(ui, world, window);
    // Everything below floats above the docked panels, and the batcher emits every
    // rectangle before any text: without a flush between the two, a floating
    // window's background lands under the console's log lines and the text shows
    // straight through it.
    ui.flush();
    drawFloatingPanels(ui, world, window);
    // Same reason, same place: the dropdowns hang over the panels AND over any
    // floating window under them.
    ui.flush();
    // The overlay pass declares its rectangles into the guard, and every widget
    // below asks the guard whether the pointer is over one - so a click on a
    // dropdown row never reaches the button underneath it, without any pass
    // having to claim the pointer by hand.
    //
    // The pass owns the surfaces it declared: a dropdown does not take the press
    // from the rows inside it.
    ui_.setOverlayOwner(kOwnerTopBarOverlay);
    drawTopBarOverlay(ui, world, camera, window, fbWidth, fbHeight);
    ui_.setOverlayOwner(0);
    // A dialog is modal over everything, including the dropdowns: without the flush
    // its dim and its background would be drawn under the labels of the menu behind
    // it, and the menu would read as if it were on top of the dialog.
    ui.flush();
    // The dialog owns the press inside it, and the whole window is in the guard
    // as its surface, so nothing under it is alive while it is up.
    ui_.setOverlayOwner(kOwnerModal);
    drawModal(ui, world, window, fbWidth, fbHeight);
    ui_.setOverlayOwner(0);
    // Above even the dialogs: the drag ghost belongs to the pointer, not to the
    // layout, and it has to be visible over the dim.
    ui.flush();
    drawPanelDragFeedback(ui);

    ui.end();
    pruneSelection(world);

    // The change that was being made is over when the body stops changing,
    // and it becomes one entry on the undo stack. The body as it is now is
    // also where the next change goes back to.
    finishUndoIfDone(world);
    if (activeBody_ >= 0 && activeBody_ < (int)world.bodies.size()) {
        bodySnapshot_ = world.bodies[(size_t)activeBody_];
        bodySnapshotIndex_ = activeBody_;
        bodySnapshotValid_ = true;
    } else {
        bodySnapshotValid_ = false;
        bodySnapshotIndex_ = -1;
    }
}

// The docked panels, in the order the eye meets them: the tree on the left, the
// inspector on the right, the console under both. A panel that is floating is not
// drawn here - it is drawn again over the scene, which is the only thing that
// makes floating look different from docking.
void Editor::drawDockedPanels(UiRenderer& ui, PhysicsWorld& world, Window& window) {
    // The docked panels are the bottom of the window: they are under the
    // floating ones, and the floating ones are in the guard, so a click on a
    // floating panel does not reach the tree below it.
    ui_.setOverlayOwner(0);
    for (int i = 0; i < kPanelCount; ++i) {
        if (!panels_[i].open || panels_[i].zone == PanelZone::Float) continue;
        drawPanelById(ui, world, window, (PanelId)i);
    }
}

// The floating panels, in the order they are drawn: the one under the pointer
// last, so that the window the pointer is aiming at is the window on top.
int Editor::floatingPanelOrder(int* out) const {
    int count = 0;
    for (int i = 0; i < kPanelCount; ++i)
        if (panels_[i].zone == PanelZone::Float && panels_[i].open) out[count++] = i;

    // The panel under the pointer comes to the front. Two windows that cover
    // each other are two windows where one of them cannot be reached, and the
    // one the pointer is on is the one the pointer is on.
    for (int i = 0; i < count; ++i) {
        const Rect rect = panels_[out[i]].floatRect;
        if (!rect.contains(ui_.pointerX(), ui_.pointerY())) continue;
        const int panel = out[i];
        for (int j = i; j > 0; --j) out[j] = out[j - 1];
        out[0] = panel;
        break;
    }
    return count;
}

// The floating panels, declared into the overlay guard.
//
// The guard is what says "this press belongs to that surface", and a floating
// panel is a surface: it is over the scene and over the panels below it, and
// a click on it must not pick the body that is behind it or press the button
// of the panel it is covering.
void Editor::declareFloatingPanels() {
    int order[kPanelCount];
    const int count = floatingPanelOrder(order);
    for (int i = 0; i < count; ++i) {
        const int panel = order[i];
        const Rect rect = panels_[panel].floatRect;
        if (rect.width <= 0.0f || rect.height <= 0.0f) continue;
        ui_.overlays().push(OverlayRect{rect.x, rect.y, rect.width, rect.height, true,
                                         kOwnerFloatingPanel + panel});
    }
}

void Editor::drawFloatingPanels(UiRenderer& ui, PhysicsWorld& world, Window& window) {
    int order[kPanelCount];
    const int count = floatingPanelOrder(order);
    for (int i = 0; i < count; ++i) {
        const int panel = order[i];
        // While it is being dragged the ghost is drawn instead, so the panel does
        // not run ahead of the pointer.
        if (dragPanel_ == panel) continue;
        // The panel owns the press inside it: the guard does not take the press
        // from the widgets of the surface it belongs to.
        ui_.setOverlayOwner(kOwnerFloatingPanel + panel);
        drawPanelById(ui, world, window, (PanelId)panel);
    }
    ui_.setOverlayOwner(0);
}

void Editor::drawPanelById(UiRenderer& ui, PhysicsWorld& world, Window& window, PanelId id) {
    const Rect panel = panelRect(id);
    ui.fillRect(panel.x, panel.y, panel.width, panel.height, theme::panel);
    switch (id) {
    case PanelId::Hierarchy: drawHierarchyDock(ui, world, window, panel); break;
    case PanelId::Inspector: drawInspectorDock(ui, world, window, panel); break;
    case PanelId::Console: drawConsolePanel(ui, panel); break;
    case PanelId::Favorites: drawFavoritesDock(ui, world, panel); break;
    case PanelId::Sounds: drawSoundsDock(ui, world, panel); break;
    case PanelId::Landform: drawLandformPanel(ui, world, panel); break;
    case PanelId::PanelCount: break;
    }
    drawPanelChrome(ui, id);
}

} // namespace s3d