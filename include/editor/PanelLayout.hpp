#pragma once
#include "render/Camera.hpp"  // Rect

namespace s3d {

// Where a panel can be: the two columns, the three places along the bottom, and
// free float over the scene.
//
// The corners are separate zones rather than "the left edge, but lower", because
// a panel that owns the bottom-left corner and a panel that owns the left edge
// both exist at once - the corner is where the console stops covering the
// hierarchy, and without its own zone there is nowhere to put a second list once
// the first one has the bottom strip.
//
// `Float` is last on purpose: several tables are sized `(int)PanelZone::Float` and
// index them by zone.
enum class PanelZone { Left, Right, Bottom, BottomLeft, BottomRight, Float };

// Which side of the window a zone sits on. The grip, the resize and the layout
// file all have to ask the same question, and three comparisons per site is three
// places to forget the third one.
constexpr bool zoneIsBottom(PanelZone zone) {
    return zone == PanelZone::Bottom || zone == PanelZone::BottomLeft ||
           zone == PanelZone::BottomRight;
}
constexpr bool zoneIsLeft(PanelZone zone) {
    return zone == PanelZone::Left || zone == PanelZone::BottomLeft;
}
constexpr bool zoneIsRight(PanelZone zone) {
    return zone == PanelZone::Right || zone == PanelZone::BottomRight;
}

// How many panels the editor has: the hierarchy, the inspector, the console, the
// favorites, the sounds and the landform.
inline constexpr int kPanelCount = 6;

// The bounds for every panel size, wherever the size came from: a drag, a layout
// file, a hand-edited one. Shared rather than repeated, because "too small to be
// a panel" has to mean the same number in all three places.
inline constexpr float kPanelMinWidth = 160.0f;
inline constexpr float kPanelMinHeight = 88.0f;
inline constexpr float kDockMinWidth = 180.0f;
inline constexpr float kDockMaxWidth = 520.0f;
inline constexpr float kConsoleMinHeight = 80.0f;

// THE WINDOW AS NUMBERS
//
// The sizes the user chose, where every panel is and whether it is open: the
// whole input of the layout and nothing else.
//
// Plain data on purpose. The editor fills one in every frame and the geometry is
// computed from it, and a test can build one and ask the same questions - with
// no window and no editor. That is the only way a number which decides where a
// panel sits ever gets checked: the layout used to be found by looking at it.
struct Workspace {
    // The framebuffer the layout is computed for.
    int fbWidth = 0;
    int fbHeight = 0;

    // The sizes the user chose.
    float leftWidth = 260.0f;
    float rightWidth = 280.0f;
    float bottomHeight = 156.0f;
    float bottomLeftHeight = 156.0f;
    float bottomRightHeight = 156.0f;

    // The zone of every panel, and whether it is open. The order is the panel
    // order: hierarchy, inspector, console, favorites, sounds.
    PanelZone zones[kPanelCount] = {PanelZone::Left, PanelZone::Right, PanelZone::Bottom,
                                    PanelZone::BottomLeft, PanelZone::BottomRight};
    bool open[kPanelCount] = {true, true, true, true, true};

    // Where a floating panel is. It is part of the layout and it is saved with
    // it: a panel that floats and comes back floating comes back where it was.
    Rect floatRects[kPanelCount] = {};

    // The panel being dragged and the zone it would land in. The layout it will
    // have when it lands, so the drop preview is measured against the same thing
    // the drag will produce.
    int dragPanel = -1;
    PanelZone dropZone = PanelZone::Left;
};

// The rectangles the window is made of.
struct LayoutRects {
    Rect menuBar;
    Rect viewport;
    Rect sceneHeader;    // "Scene" strip across the top of the viewport
    Rect viewportTools;  // vertical tool column inside the viewport, on the left
};

// Where the panel in `zone` sits. A zone nobody occupies is an empty rectangle
// at the origin, so a panel that is floating does not keep an invisible claim
// on the edge it came from.
Rect zoneRect(const Workspace& ws, PanelZone zone);

// The whole window: the scene column, the strip above it and the tools in it.
LayoutRects computeLayout(const Workspace& ws);

// Which edges of a panel the pointer is on, as a set of edge bits, or 0 for
// none. The bits the drag uses.
enum : unsigned {
    kGripLeft = 1u,
    kGripRight = 2u,
    kGripTop = 4u,
    kGripBottom = 8u,
};

// The grip of a docked panel is the edge it has against the scene; a floating
// panel has all four. The pointer has to be over THAT edge of THAT panel: a grip
// that answered to a whole line of the screen lit up over panels that were not
// there, and the line came and went in pieces as the pointer crossed them.
unsigned gripsAt(const Workspace& ws, int panel, float x, float y);

// The sizes after a grip has been dragged: the one the grip moves, and the
// others as they were. `dx` and `dy` are how far the pointer moved.
//
// What a grip moves is decided by the GRIP, not by the zone the panel is in: the
// panel in the bottom-left corner has two - its top edge, which makes it
// taller, and its right edge, which is the edge of the column it shares with the
// panel above it.
struct WorkspaceSizes {
    float leftWidth = 0.0f;
    float rightWidth = 0.0f;
    float bottomHeight = 0.0f;
    float bottomLeftHeight = 0.0f;
    float bottomRightHeight = 0.0f;
};
WorkspaceSizes applyResize(const Workspace& ws, int panel, unsigned edges, float startSize,
                           float dx, float dy);

} // namespace s3d
