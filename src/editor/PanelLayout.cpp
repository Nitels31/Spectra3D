#include "editor/PanelLayout.hpp"

#include "core/Math.hpp"
#include "ui/Theme.hpp"

#include <algorithm>
#include <cmath>

namespace s3d {
namespace {

// How close the pointer has to be to an edge to grab it. A docked panel has one
// edge, the divider against the scene; a floating one has four, plus the
// corners. The corner box is the larger of the two on purpose: inside it the
// pointer is taking two edges at once, and taking only one of them is what
// makes a window feel as if it slipped.
constexpr float kDockGrip = 6.0f;
constexpr float kFloatGrip = 5.0f;
constexpr float kFloatCorner = 14.0f;


// The strip above the viewport, and the vertical tool column inside it.
constexpr float kSceneHeaderHeight = 26.0f;
constexpr float kToolStripWidth = 34.0f;
constexpr float kMinViewportSize = 120.0f;

} // namespace

Rect zoneRect(const Workspace& ws, PanelZone zone) {
    if (zone == PanelZone::Float) return Rect{};

    const float width = (float)ws.fbWidth;
    const float height = (float)ws.fbHeight;
    const float top = theme::menuBarHeight;

    // Occupancy, not identity: a column or the bottom row is only as wide as it is
    // because of what else is docked in it, so a floating panel must not keep an
    // invisible claim on the space it used to have. Closed panels are not there
    // either - "where is anything docked?" is a question about what is on screen.
    //
    // The zone a panel is being dragged towards counts as occupied too, or the drop
    // preview would be measured against a layout that does not exist yet: the panel
    // arriving in the bottom-left corner has not landed, and without this it would
    // be previewed as a rectangle of zero width. The dragged panel is left in the
    // loop as well - a drag that leaves a column has not vacated it yet, which is
    // exactly what the frame it is previewed in shows.
    bool leftTaken = ws.dragPanel >= 0 && zoneIsLeft(ws.dropZone);
    bool rightTaken = ws.dragPanel >= 0 && zoneIsRight(ws.dropZone);
    bool bottomLeftTaken = ws.dragPanel >= 0 && ws.dropZone == PanelZone::BottomLeft;
    bool bottomRightTaken = ws.dragPanel >= 0 && ws.dropZone == PanelZone::BottomRight;
    for (int i = 0; i < kPanelCount; ++i) {
        if (!ws.open[i]) continue;
        const PanelZone other = ws.zones[i];
        leftTaken = leftTaken || zoneIsLeft(other);
        rightTaken = rightTaken || zoneIsRight(other);
        bottomLeftTaken = bottomLeftTaken || other == PanelZone::BottomLeft;
        bottomRightTaken = bottomRightTaken || other == PanelZone::BottomRight;
    }

    // Two columns and three panels along the bottom, and every one of them has a
    // height of its own.
    //
    // Each column ends where the panel in its own corner begins: the hierarchy
    // is above the favourites panel, the inspector is above the sounds one, and
    // the scene is above the console. That is the whole rule, and it is what
    // keeps the panels from moving each other around - the bottom is not one
    // strip split in three, so dragging one panel's edge changes that panel and
    // nothing else. A row that shared one height made the favourites panel
    // reach up and shorten the inspector on the other side of the window.
    const float leftW = leftTaken ? ws.leftWidth : 0.0f;
    const float rightW = rightTaken ? ws.rightWidth : 0.0f;

    // The bottom of each column: the top of the panel standing in its corner, or
    // the bottom of the window when the corner is empty. A closed panel reserves
    // nothing - that is the whole point of closing it.
    const float leftBottom = bottomLeftTaken ? height - ws.bottomLeftHeight : height;
    const float rightBottom = bottomRightTaken ? height - ws.bottomRightHeight : height;
    // The width of the middle column, where the scene and the console are.
    const float middleW = std::fmax(0.0f, width - leftW - rightW);

    switch (zone) {
    case PanelZone::Left:
        return Rect{0.0f, top, leftW, std::fmax(0.0f, leftBottom - top)};
    case PanelZone::Right:
        return Rect{width - rightW, top, rightW, std::fmax(0.0f, rightBottom - top)};
    case PanelZone::BottomLeft:
        return Rect{0.0f, height - ws.bottomLeftHeight, leftW, ws.bottomLeftHeight};
    case PanelZone::BottomRight:
        return Rect{width - rightW, height - ws.bottomRightHeight, rightW, ws.bottomRightHeight};
    case PanelZone::Bottom:
        return Rect{leftW, height - ws.bottomHeight, middleW, ws.bottomHeight};
    case PanelZone::Float:
        break;
    }
    return Rect{};
}

LayoutRects computeLayout(const Workspace& ws) {
    LayoutRects out;
    const float width = (float)ws.fbWidth;
    const float height = (float)ws.fbHeight;

    out.menuBar = Rect{0.0f, 0.0f, width, theme::menuBarHeight};

    // Which columns and rows are occupied decides the scene: a floating inspector
    // gives its space back to the viewport, and an empty left column moves the
    // scene to x=0. Corner zones occupy the column they are in, which is what
    // keeps the bottom-left corner from becoming a hole in the layout.
    bool leftTaken = false, rightTaken = false, consoleTaken = false;
    for (int i = 0; i < kPanelCount; ++i) {
        if (!ws.open[i]) continue;
        const PanelZone zone = ws.zones[i];
        leftTaken = leftTaken || zoneIsLeft(zone);
        rightTaken = rightTaken || zoneIsRight(zone);
        consoleTaken = consoleTaken || zone == PanelZone::Bottom;
    }

    const float sceneLeft = leftTaken ? ws.leftWidth : 0.0f;
    const float sceneRight = rightTaken ? ws.rightWidth : 0.0f;
    const float columnTop = theme::menuBarHeight;

    // The scene reaches down to the console, and to the bottom of the window
    // when there is no console. The corners stand below the columns, not in the
    // middle, so they do not decide how much of the scene is left - the console
    // alone does, and the two corner panels can be as tall as they like.
    const float sceneBottom = consoleTaken ? height - ws.bottomHeight : height;

    const float viewX = sceneLeft;
    const float viewW = std::fmax(kMinViewportSize, width - sceneLeft - sceneRight);
    out.sceneHeader = Rect{viewX, columnTop, viewW, kSceneHeaderHeight};
    out.viewport = Rect{viewX, columnTop + kSceneHeaderHeight, viewW,
                        std::fmax(kMinViewportSize, sceneBottom - columnTop - kSceneHeaderHeight)};
    out.viewportTools = Rect{out.viewport.x + 4.0f, out.viewport.y + 4.0f, kToolStripWidth, 0.0f};
    out.viewportTools.height = std::max(0.0f, out.viewport.height - 8.0f);
    return out;
}

unsigned gripsAt(const Workspace& ws, int panel, float x, float y) {
    if (panel < 0 || panel >= kPanelCount || !ws.open[panel]) return 0;

    const PanelZone zone = ws.zones[panel];
    const Rect rect = zone == PanelZone::Float ? ws.floatRects[panel] : zoneRect(ws, zone);
    if (rect.width <= 0.0f || rect.height <= 0.0f) return 0;

    if (zone != PanelZone::Float) {
        // The whole edge counts, not just the part below the title bar: for
        // the bottom row the grip IS the first line of the title bar, and a grip
        // that stopped where the title bar starts would leave the console with
        // nowhere to resize from. Every bottom zone - the strip and both corners
        // - resizes the same row, so they all take the same edge.
        //
        // And the grip belongs to THIS panel: the pointer has to be over this
        // edge and no other. A grip that answered to a whole line of the screen
        // lit up over panels that were not there at all.
        const bool overX = x >= rect.x - kDockGrip && x <= rect.x + rect.width + kDockGrip;
        const bool overY = y >= rect.y - kDockGrip && y <= rect.y + rect.height + kDockGrip;
        if (zoneIsLeft(zone) && overY && std::fabs(x - (rect.x + rect.width)) <= kDockGrip)
            return kGripRight;
        if (zoneIsRight(zone) && overY && std::fabs(x - rect.x) <= kDockGrip) return kGripLeft;
        if (zoneIsBottom(zone) && overX && std::fabs(y - rect.y) <= kDockGrip) return kGripTop;
        return 0;
    }

    // A floating panel is resized from any of its edges and from its corners.
    // The rectangle of a floating panel is the editor's business - it is the only
    // thing here that is - and the test works with the zone rectangles.
    //
    // And the edge belongs to the panel: the pointer has to be over the panel's
    // own edge, not over a line that goes through the whole screen. A grip that
    // answered to the whole line resized a window that was somewhere else
    // entirely, and the drag of a window's title bar was taken away by the
    // edge of another window standing on the same line.
    if (x < rect.x - kFloatGrip || x > rect.x + rect.width + kFloatGrip) return 0;
    if (y < rect.y - kFloatGrip || y > rect.y + rect.height + kFloatGrip) return 0;
    const float left = std::fabs(x - rect.x);
    const float right = std::fabs(x - (rect.x + rect.width));
    const float top = std::fabs(y - rect.y);
    const float bottom = std::fabs(y - (rect.y + rect.height));
    const float nearX = left <= kFloatCorner || right <= kFloatCorner ? kFloatCorner : kFloatGrip;
    const float nearY = bottom <= kFloatCorner ? kFloatCorner : kFloatGrip;

    unsigned grips = 0;
    if (left <= nearX) grips |= kGripLeft;
    if (right <= nearX) grips |= kGripRight;
    if (bottom <= nearY) grips |= kGripBottom;
    // The top edge is the title bar, so it moves the panel - except in the two
    // corner boxes, where it takes the side edge with it. A drag handle that a
    // resize grip steals from is not a drag handle.
    if (top <= kFloatCorner && (left <= kFloatCorner || right <= kFloatCorner))
        grips |= kGripTop;
    return grips;
}

int floatingPanelOrder(const Workspace& ws, float px, float py, int* out, int capacity) {
    int count = 0;
    for (int i = 0; i < kPanelCount && count < capacity; ++i)
        if (ws.open[i] && ws.zones[i] == PanelZone::Float) out[count++] = i;
    if (count <= 0) return 0;

    // The window under the pointer comes to the front: it is drawn last, so it
    // is the window on top, and the press on it belongs to it. The others keep
    // the order they were in - a window that is not under the pointer does not
    // change places with one that is.
    for (int i = 0; i < count; ++i) {
        if (!ws.floatRects[out[i]].contains(px, py)) continue;
        const int panel = out[i];
        for (int j = i; j + 1 < count; ++j) out[j] = out[j + 1];
        out[count - 1] = panel;
        break;
    }
    return count;
}

bool rectsOverlap(const Rect& a, const Rect& b) {
    if (a.width <= 0.0f || a.height <= 0.0f) return false;
    if (b.width <= 0.0f || b.height <= 0.0f) return false;
    // The edges touch and nothing is covered, and that is not an overlap: the
    // panels of the window stand next to each other all the time.
    return a.x < b.x + b.width && b.x < a.x + a.width &&
           a.y < b.y + b.height && b.y < a.y + a.height;
}

Rect findFreeSpot(const Workspace& ws, int panel, Rect wanted) {
    // Every other open panel is in the way: a floating window that covers the
    // tree hides the tree, and one that covers another floating window hides
    // that one too. The scene is not in the way - a floating window is a
    // window OVER the scene, and that is what it is for.
    Rect occupied[kPanelCount];
    int count = 0;
    for (int i = 0; i < kPanelCount; ++i) {
        if (i == panel || !ws.open[i]) continue;
        const Rect rect =
            ws.zones[i] == PanelZone::Float ? ws.floatRects[i] : zoneRect(ws, ws.zones[i]);
        if (rect.width <= 0.0f || rect.height <= 0.0f) continue;
        occupied[count++] = rect;
    }
    if (count == 0) return wanted;

    const auto free = [&](const Rect& rect) {
        for (int i = 0; i < count; ++i)
            if (rectsOverlap(rect, occupied[i])) return false;
        return true;
    };
    if (free(wanted)) return wanted;

    const float width = (float)ws.fbWidth;
    const float height = (float)ws.fbHeight;

    // The cascade: the same window, a little further down and to the right
    // each time. This is what a window manager does with a window that would
    // land on another one.
    constexpr float kStep = 28.0f;
    for (int i = 1; i < 24; ++i) {
        Rect rect = wanted;
        rect.x = wanted.x + (float)i * kStep;
        rect.y = wanted.y + (float)i * kStep;
        if (rect.x + rect.width > width) break;
        if (rect.y + rect.height > height) break;
        if (free(rect)) return rect;
    }

    // And then a row along the top of the window, for the windows that do not
    // fit in the cascade.
    for (int i = 0; i < 8; ++i) {
        Rect rect = wanted;
        rect.x = (float)i * (wanted.width + 8.0f) + 8.0f;
        rect.y = wanted.y;
        if (rect.x + rect.width > width) break;
        if (free(rect)) return rect;
    }

    // Nowhere free: the panel stays where it was dropped. A window that has
    // been put somewhere on purpose should not be moved away from there
    // because there was no better place.
    return wanted;
}

WorkspaceSizes applyResize(const Workspace& ws, int panel, unsigned edges, float startSize,
                           float dx, float dy) {
    WorkspaceSizes sizes;
    sizes.leftWidth = ws.leftWidth;
    sizes.rightWidth = ws.rightWidth;
    sizes.bottomHeight = ws.bottomHeight;
    sizes.bottomLeftHeight = ws.bottomLeftHeight;
    sizes.bottomRightHeight = ws.bottomRightHeight;
    if (panel < 0 || panel >= kPanelCount || edges == 0) return sizes;

    const PanelZone zone = ws.zones[panel];
    const float width = (float)ws.fbWidth;
    const float height = (float)ws.fbHeight;

    if ((edges & kGripTop) && zoneIsBottom(zone)) {
        // Each panel of the bottom row resizes itself: the console's grip does
        // not make the favourites panel taller, and the other way round. A row
        // of three panels that share one height is a row of one panel with two
        // holes in it.
        const float limit = std::fmax(kConsoleMinHeight, height * 0.7f);
        const float value = clampf(startSize - dy, kConsoleMinHeight, limit);
        if (zone == PanelZone::BottomLeft)
            sizes.bottomLeftHeight = value;
        else if (zone == PanelZone::BottomRight)
            sizes.bottomRightHeight = value;
        else
            sizes.bottomHeight = value;
    } else if ((edges & kGripRight) && zoneIsLeft(zone)) {
        sizes.leftWidth = clampf(startSize + dx, kDockMinWidth,
                                 std::fmin(kDockMaxWidth, width * 0.5f));
    } else if ((edges & kGripLeft) && zoneIsRight(zone)) {
        sizes.rightWidth = clampf(startSize - dx, kDockMinWidth,
                                  std::fmin(kDockMaxWidth, width * 0.5f));
    }
    return sizes;
}

} // namespace s3d
