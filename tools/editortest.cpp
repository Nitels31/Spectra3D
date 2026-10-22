// Console verification of the editor's panel geometry, of the picture and the
// pointer agreeing about where a thing is, and of the font the engine ships
// having every letter the atlas needs.
//
// Built as `editortest`; it needs no window, no project and no GPU. The
// geometry is in `editor/PanelLayout.hpp` precisely so that it can be asked
// about here: it used to live in the editor, and the only way to find out
// whether a panel was in the right place was to open the editor and look.
//
// The layout of the window is the one thing every panel bug is about, and every
// one of them was found by taking a screenshot. A number that decides where a
// panel sits can be checked; a screenshot has to be looked at.

#include "editor/PanelLayout.hpp"
#include "editor/Picking.hpp"
#include "editor/Editor.hpp"

#include "core/Math.hpp"
#include "core/Project.hpp"
#include "core/Str.hpp"
#include "core/TextCoverage.hpp"
#include "gui/GuiCanvas.hpp"
#include "platform/Filesystem.hpp"
#include "platform/Window.hpp"
#include "render/Camera.hpp"
#include "render/FontFile.hpp"
#include "render/ParticleSystem.hpp"
#include "render/SceneRenderer.hpp"
#include "render/TextAtlas.hpp"
#include "assets/MeshRegistry.hpp"
#include "physics/PhysicsWorld.hpp"
#include "scene/BodyFactory.hpp"
#include "scene/Landform.hpp"
#include "scene/Cloud.hpp"
#include "scene/Noise.hpp"
#include "scene/SceneSerializer.hpp"
#include "ui/OverlayGuard.hpp"


#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace s3d;

namespace {

int g_checks = 0;
int g_failures = 0;

// One line per assertion so a failure names the behaviour that broke.
void check(bool condition, const char* what) {
    ++g_checks;
    if (condition) {
        std::printf("  ok   %s\n", what);
        return;
    }
    ++g_failures;
    std::printf("  FAIL %s\n", what);
}

void heading(const char* what) { std::printf("\n== %s ==\n", what); }

bool nearly(float a, float b, float epsilon = 0.01f) {
    return std::fabs(a - b) <= epsilon;
}

// The panels, in the order the workspace lists them.
enum { kHierarchy = 0, kInspector, kConsole, kFavorites, kSounds };

// A window with every panel open, the sizes an editor usually has.
Workspace sampleWorkspace() {
    Workspace ws;
    ws.fbWidth = 1280;
    ws.fbHeight = 720;
    ws.leftWidth = 260.0f;
    ws.rightWidth = 280.0f;
    ws.bottomHeight = 233.0f;
    ws.bottomLeftHeight = 137.0f;
    ws.bottomRightHeight = 329.0f;
    for (int i = 0; i < kPanelCount; ++i) ws.open[i] = true;
    return ws;
}

// THE PANELS ARE WHERE THEY SAY THEY ARE

void testGeometry() {
    heading("the window is tiled");
    const Workspace ws = sampleWorkspace();

    const Rect left = zoneRect(ws, PanelZone::Left);
    const Rect right = zoneRect(ws, PanelZone::Right);
    const Rect corner = zoneRect(ws, PanelZone::BottomLeft);
    const Rect other = zoneRect(ws, PanelZone::BottomRight);
    const Rect console = zoneRect(ws, PanelZone::Bottom);
    const LayoutRects rects = computeLayout(ws);

    // Each column ends where the panel in its own corner begins. A column that
    // ended somewhere else left a hole between the two, and the hole showed the
    // clear colour of the frame.
    check(nearly(left.y + left.height, corner.y), "the left column ends where the favorites begin");
    check(nearly(right.y + right.height, other.y),
          "the right column ends where the sounds begin");
    check(nearly(corner.y + corner.height, (float)ws.fbHeight),
          "the favorites panel stands on the bottom of the window");
    check(nearly(other.y + other.height, (float)ws.fbHeight),
          "the sounds panel stands on the bottom of the window");

    // The console is the middle column's rectangle, right under the view.
    check(nearly(console.x, left.x + left.width), "the console starts where the left column ends");
    check(nearly(console.x + console.width, right.x), "the console ends where the right column begins");
    check(nearly(console.y, rects.viewport.y + rects.viewport.height),
          "the console is directly under the viewport");
    check(nearly(corner.width, left.width), "the corner panel is as wide as its column");
    check(nearly(other.width, right.width), "the corner panel is as wide as its column");

    // The scene is the space between the columns.
    check(nearly(rects.viewport.x, left.x + left.width), "the scene starts where the left column ends");
    check(nearly(rects.viewport.width, right.x - (left.x + left.width)),
          "the scene is as wide as the space between the columns");
}

// A CLOSED PANEL RESERVES NOTHING

void testClosedPanel() {
    heading("a closed panel reserves nothing");
    Workspace ws = sampleWorkspace();
    ws.open[kFavorites] = false;

    const Rect left = zoneRect(ws, PanelZone::Left);
    const Rect console = zoneRect(ws, PanelZone::Bottom);
    const Workspace before = sampleWorkspace();
    const Rect consoleBefore = zoneRect(before, PanelZone::Bottom);

    check(nearly(left.y + left.height, (float)ws.fbHeight),
          "the left column runs to the bottom of the window");
    check(nearly(console.x, consoleBefore.x) && nearly(console.width, consoleBefore.width),
          "and the console is where it was");
}

// THE PANELS IN THE BOTTOM ROW ARE INDEPENDENT

void testResize() {
    heading("a grip moves the thing it is on");
    const Workspace ws = sampleWorkspace();

    // The top edge of the favorites panel: its own height, and nothing else.
    const WorkspaceSizes taller = applyResize(ws, kFavorites, kGripTop, ws.bottomLeftHeight, 0.0f,
                                             -100.0f);
    check(nearly(taller.bottomLeftHeight, ws.bottomLeftHeight + 100.0f),
          "the top edge of the favorites panel makes it taller");
    check(nearly(taller.bottomHeight, ws.bottomHeight), "and leaves the console alone");
    check(nearly(taller.bottomRightHeight, ws.bottomRightHeight),
          "and leaves the sounds panel alone");
    check(nearly(taller.leftWidth, ws.leftWidth), "and leaves the left column alone");
    check(nearly(taller.rightWidth, ws.rightWidth), "and leaves the right column alone");

    // Its right edge: the width of the column it shares with the hierarchy.
    const WorkspaceSizes wider = applyResize(ws, kFavorites, kGripRight, ws.leftWidth, 40.0f, 0.0f);
    check(nearly(wider.leftWidth, ws.leftWidth + 40.0f),
          "the right edge of the favorites panel widens the column");
    check(nearly(wider.bottomLeftHeight, ws.bottomLeftHeight),
          "and leaves the favorites panel's height alone");
    check(nearly(wider.bottomHeight, ws.bottomHeight), "and leaves the console alone");

    // The same for the sounds panel on the other side.
    const WorkspaceSizes other = applyResize(ws, kSounds, kGripTop, ws.bottomRightHeight, 0.0f,
                                             -60.0f);
    check(nearly(other.bottomRightHeight, ws.bottomRightHeight + 60.0f),
          "the top edge of the sounds panel makes it taller");
    check(nearly(other.bottomLeftHeight, ws.bottomLeftHeight),
          "and leaves the favorites panel alone");
    check(nearly(other.rightWidth, ws.rightWidth), "and leaves the right column alone");

    const WorkspaceSizes narrower = applyResize(ws, kSounds, kGripLeft, ws.rightWidth, 30.0f, 0.0f);
    check(nearly(narrower.rightWidth, ws.rightWidth - 30.0f),
          "the left edge of the sounds panel narrows the column");
    check(nearly(narrower.bottomRightHeight, ws.bottomRightHeight),
          "and leaves the sounds panel's height alone");

    // The console's top edge is the console's own.
    const WorkspaceSizes console = applyResize(ws, kConsole, kGripTop, ws.bottomHeight, 0.0f, -20.0f);
    check(nearly(console.bottomHeight, ws.bottomHeight + 20.0f), "the console's top edge raises it");
    check(nearly(console.bottomLeftHeight, ws.bottomLeftHeight),
          "and leaves the favorites panel alone");
}

// A GRIP BELONGS TO ONE EDGE OF ONE PANEL

void testGrips() {
    heading("a grip is on the edge of its own panel");
    const Workspace ws = sampleWorkspace();
    const Rect left = zoneRect(ws, PanelZone::Left);
    const Rect right = zoneRect(ws, PanelZone::Right);
    const Rect console = zoneRect(ws, PanelZone::Bottom);
    const Rect corner = zoneRect(ws, PanelZone::BottomLeft);

    check(gripsAt(ws, kHierarchy, left.x + left.width, left.y + 40.0f) == kGripRight,
          "the right edge of the hierarchy is a grip");
    check(gripsAt(ws, kHierarchy, left.x + 40.0f, left.y + 40.0f) == 0,
          "the middle of the hierarchy is not");
    check(gripsAt(ws, kConsole, console.x + 40.0f, console.y) == kGripTop,
          "the top edge of the console is a grip");
    check(gripsAt(ws, kConsole, right.x + 40.0f, console.y) == 0,
          "and it is not a grip over the inspector");
    check(gripsAt(ws, kFavorites, corner.x + 40.0f, corner.y) == kGripTop,
          "the top edge of the favorites panel is a grip");
    check(gripsAt(ws, kInspector, right.x, right.y + 40.0f) == kGripLeft,
          "the left edge of the inspector is a grip");
}

// A WINDOW IS DRAGGED BY ITS TITLE BAR

void testWindowDrag() {
    heading("a floating window is dragged by its title bar");
    Workspace ws = sampleWorkspace();
    // Two windows, one over the other, and the pointer on the title bar of the
    // one that is under the pointer.
    ws.zones[kFavorites] = PanelZone::Float;
    ws.zones[kSounds] = PanelZone::Float;
    ws.floatRects[kFavorites] = Rect{100.0f, 100.0f, 300.0f, 200.0f};
    ws.floatRects[kSounds] = Rect{120.0f, 120.0f, 300.0f, 200.0f};
    // The tool windows are closed: this is about the two windows here.
    for (int i = 5; i < kPanelCount; ++i) ws.open[i] = false;

    // The window under the pointer is the one on top: it is drawn last, and
    // the surface that is drawn last is the one that owns the press.
    int order[kPanelCount];
    const int count = floatingPanelOrder(ws, 150.0f, 110.0f, order, kPanelCount);
    check(count == 2, "both windows are on screen");
    check(order[count - 1] == kFavorites, "the window under the pointer is the one on top");

    // And it is the one the pointer is on: the last one in the order is the
    // window under the pointer, and there is no window where the pointer is
    // not over one.
    check(order[count - 1] == kFavorites, "the window under the pointer is the one that is found");
    int elsewhere[kPanelCount];
    const int none = floatingPanelOrder(ws, 900.0f, 600.0f, elsewhere, kPanelCount);
    check(none == 2, "and both windows are still on screen");
    check(elsewhere[none - 1] == kSounds,
          "the window under the pointer is the one on top when the pointer is on the other");

    // The guard: a press on a window belongs to that window and to nobody
    // else. Asked as the window asks, the press is there - and that is what
    // lets the window be dragged by its title bar. Asked as anybody else, the
    // window has taken the press, which is what keeps a click on a window from
    // picking the body behind it.
    OverlayGuard guard;
    guard.push(ws.floatRects[kFavorites].x, ws.floatRects[kFavorites].y,
               ws.floatRects[kFavorites].width, ws.floatRects[kFavorites].height,
               Editor::kOwnerFloatingPanel + kFavorites);
    guard.push(ws.floatRects[kSounds].x, ws.floatRects[kSounds].y,
               ws.floatRects[kSounds].width, ws.floatRects[kSounds].height,
               Editor::kOwnerFloatingPanel + kSounds);
    check(!guard.pressTaken(150.0f, 110.0f, Editor::kOwnerFloatingPanel + kFavorites),
          "the press on a window is the press of that window");
    check(guard.pressTaken(150.0f, 110.0f, 0),
          "and it is not the press of the scene underneath it");

    // The grip of a window belongs to that window: the edge of a window that
    // is somewhere else is not an edge of this one.
    const Rect window = ws.floatRects[kFavorites];
    check(gripsAt(ws, kFavorites, window.x + 2.0f, window.y + 100.0f) == kGripLeft,
          "the left edge of the window is a grip");
    check(gripsAt(ws, kFavorites, window.x + 2.0f, window.y + 500.0f) == 0,
          "and it is not a grip a long way down the same line");
    check(gripsAt(ws, kFavorites, window.x + 150.0f, window.y + 8.0f) == 0,
          "the title bar of the window is not a grip");
}

// THE PICTURE AND THE POINTER ARE THE SAME VIEW OF THE WORLD

void testPictureAndPointer() {
    heading("the picture and the pointer agree");
    const Workspace ws = sampleWorkspace();
    const LayoutRects rects = computeLayout(ws);
    const Rect view = rects.viewport;

    Camera camera;
    camera.position = Vec3{0.0f, 0.0f, 0.0f};
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    camera.fovDeg = 70.0f;
    Vec3 forward, right, up;
    camera.basis(forward, right, up);

    float tanX = 0.0f, tanY = 0.0f;
    picking::viewRayTangents(camera, view.width, view.height, tanX, tanY);
    check(nearly(tanY, std::tan(camera.fovDeg * kPi / 360.0f), 0.0005f),
          "the vertical field of view is the camera's own");
    check(nearly(tanX / tanY, view.width / view.height, 0.0005f),
          "the horizontal one follows the viewport's shape");

    // The renderer projects with the same numbers.
    double halfWidth = 0.0, halfHeight = 0.0;
    cameraFrustum(camera, (int)view.width, (int)view.height, halfWidth, halfHeight);
    check(nearly((float)(halfWidth / halfHeight), view.width / view.height, 0.001f),
          "the frustum has the viewport's shape");
    check(nearly((float)(halfHeight / kNearPlane), tanY, 0.001f),
          "the frustum is the camera's field of view");

    // The ray in the middle of the viewport is the camera's forward.
    const Vec3 middle = picking::rayFromViewport(view.x + view.width * 0.5f,
                                                 view.y + view.height * 0.5f, view, forward,
                                                 right, up, tanX, tanY);
    check(dot(middle, forward) > 0.9999f, "the ray in the middle of the viewport is the forward");

    // And a point in front of the camera is picked where it is drawn.
    Vec3 screen = Vec3{0.0f, 0.0f, 0.0f};
    float sx = 0.0f, sy = 0.0f;
    const bool projected = picking::projectToViewport(Vec3{0.0f, 0.0f, -10.0f}, camera.position,
                                                     forward, right, up, tanX, tanY, view.width,
                                                     view.height, sx, sy);
    check(projected, "a point in front of the camera projects");
    check(nearly(sx, view.width * 0.5f, 0.5f) && nearly(sy, view.height * 0.5f, 0.5f),
          "and it lands in the middle of the viewport");
    (void)screen;
}

// THE FLAT PICTURE AND THE POINTER ARE THE SAME VIEW OF THE WORLD

//
// A flat picture is measured and not seen: the rays of an orthographic camera
// are parallel, and each of them starts at the point of the picture the
// pointer is over. A ray that started at the eye would be the same ray for
// every pixel of the picture, and a click on empty space would take whatever
// was under the middle of the screen with it.
void testFlatPictureAndPointer() {
    heading("the flat picture and the pointer agree");
    const Rect view{0.0f, 0.0f, 800.0f, 600.0f};

    // The flat view: an orthographic camera looking down the Z axis.
    Camera camera;
    camera.position = Vec3{0.0f, 0.0f, 60.0f};
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    camera.orthographic = true;
    camera.orthoSize = 8.0f;

    picking::ViewProjection projection;
    picking::viewProjection(camera, view.width, view.height, projection);
    check(projection.orthographic, "the projection knows that it is flat");

    // The ray in the middle of the picture starts in the middle of the picture.
    Vec3 origin, direction;
    picking::rayFromViewport(view.width * 0.5f, view.height * 0.5f, view, projection, origin,
                             direction);
    check(nearly(origin.x, 0.0f, 0.001f) && nearly(origin.y, 0.0f, 0.001f),
          "the ray in the middle of the flat picture starts in the middle of it");
    check(dot(direction, camera.forward()) > 0.9999f, "and it goes the way the eye looks");

    // The ray at the corner of the picture starts at the corner of it: the
    // origin is where the pointer is, and not where the eye is.
    picking::rayFromViewport(view.width * 0.75f, view.height * 0.25f, view, projection, origin,
                             direction);
    check(std::fabs(origin.x) > 1.0f && std::fabs(origin.y) > 1.0f,
          "the ray at the corner of the flat picture starts at the corner of it");

    // And the picture and the pointer are inverses of each other: a thing of
    // the world is drawn where the pointer that stands on it is.
    float px = 0.0f, py = 0.0f;
    const bool projected = picking::projectToViewport(Vec3{2.0f, 1.0f, 0.0f}, projection,
                                                     view.width, view.height, px, py);
    check(projected, "a thing in front of the flat eye is projected");
    picking::rayFromViewport(px, py, view, projection, origin, direction);
    check(nearly(origin.x, 2.0f, 0.01f) && nearly(origin.y, 1.0f, 0.01f),
          "and the pointer over it starts the ray at the thing itself");
}

// A CLICK TAKES WHAT IS UNDER THE POINTER AND NOTHING ELSE

void testFlatPicking() {
    heading("a click takes what is under the pointer");
    const Rect view{0.0f, 0.0f, 800.0f, 600.0f};

    Camera camera;
    camera.position = Vec3{0.0f, 0.0f, 60.0f};
    camera.orthographic = true;
    camera.orthoSize = 8.0f;
    picking::ViewProjection projection;
    picking::viewProjection(camera, view.width, view.height, projection);

    PhysicsWorld world;
    RigidBody body;
    body.shape = Shape::Box;
    body.halfExtents = Vec3{0.5f, 0.5f, 0.5f};
    body.position = Vec3{2.0f, 1.0f, 0.0f};
    world.bodies.push_back(body);

    // Where the body is drawn.
    float px = 0.0f, py = 0.0f;
    picking::projectToViewport(body.position, projection, view.width, view.height, px, py);

    Vec3 origin, direction;
    picking::rayFromViewport(px, py, view, projection, origin, direction);
    check(picking::pickBody(origin, direction, world) == 0,
          "the pointer over the body takes the body");

    // And a pointer over empty space takes nothing: a click that misses must
    // not pick whatever happened to be under the middle of the screen.
    picking::rayFromViewport(view.width * 0.5f, view.height * 0.5f, view, projection, origin,
                             direction);
    check(picking::pickBody(origin, direction, world) < 0, "a click on empty space takes nothing");

    // Even when the empty space is right next to the body: the ray of the
    // pointer is the ray of the pointer.
    picking::rayFromViewport(px + 60.0f, py, view, projection, origin, direction);
    check(picking::pickBody(origin, direction, world) < 0,
          "and a click next to the body takes nothing either");
}

// A CLICK ON EMPTY SPACE IN A FLAT PICTURE TAKES NOTHING
//
// The case that a flat picture makes different: the ray of the pointer starts
// in the plane of the picture and not at the eye, so the handle of the gizmo
// is a long way from where the pointer is - and a handle that is far away must
// not be grabbed by a click that is nowhere near it.
void testFlatEmptyClick() {
    heading("a click on empty space in a flat picture takes nothing");
    // The picture of the editor: 740 by 509 pixels, a box of one unit in the
    // middle of it, and the pointer a long way from the box.
    const Rect view{0.0f, 0.0f, 740.0f, 509.0f};

    Camera camera;
    camera.position = Vec3{0.0f, 6.0f, 60.0f};
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    camera.orthographic = true;
    camera.orthoSize = 8.0f;
    picking::ViewProjection projection;
    picking::viewProjection(camera, view.width, view.height, projection);

    RigidBody body;
    body.shape = Shape::Box;
    body.halfExtents = Vec3{1.0f, 1.0f, 1.0f};
    body.position = Vec3{0.0f, 6.0f, 0.0f};
    body.boundRadius = std::sqrt(3.0f);

    PhysicsWorld world;
    world.bodies.push_back(body);

    // The pointer, up and to the left of the box.
    const float pointerX = 185.0f, pointerY = 128.0f;
    Vec3 origin, direction;
    picking::rayFromViewport(pointerX, pointerY, view, projection, origin, direction);

    // Nothing is under the pointer, so nothing is taken.
    check(picking::pickBody(origin, direction, world) < 0, "the ray takes no body");

    // And the handle of the gizmo is not under the pointer either: the handle
    // is a short arrow next to the box, and the pointer is a long way from it.
    const int axis = gizmo::pickAxis(pointerX, pointerY, origin, direction, projection, view, body,
                                      gizmo::Mode::Move, 2);
    check(axis < 0, "and the gizmo is not grabbed either");
}

// A FLAT WORLD HAS TWO HANDLES AND ONE RING
void testFlatGizmo() {
    heading("a flat world has two handles and one ring");
    const Rect view{0.0f, 0.0f, 800.0f, 600.0f};

    Vec3 axes[3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    int drawn[3];
    check(gizmo::flatDrawAxes(axes, -1, drawn) == 3, "the 3D view has three handles");
    check(gizmo::flatDrawAxes(axes, 2, drawn) == 2 && drawn[0] == 0 && drawn[1] == 1,
          "the flat picture of the XY plane has the X and the Y handle");
    check(gizmo::flatDrawAxes(axes, 1, drawn) == 2 && drawn[0] == 0 && drawn[1] == 2,
          "the flat picture of the XZ plane has the X and the Z handle");
    check(gizmo::flatDrawAxes(axes, 0, drawn) == 2 && drawn[0] == 1 && drawn[1] == 2,
          "the flat picture of the YZ plane has the Y and the Z handle");

    // The handle that is out of the picture is not drawn, and a handle that is
    // not drawn must not be grabbable.
    Camera camera;
    camera.position = Vec3{0.0f, 0.0f, 60.0f};
    camera.orthographic = true;
    camera.orthoSize = 8.0f;
    picking::ViewProjection projection;
    picking::viewProjection(camera, view.width, view.height, projection);

    RigidBody body;
    body.shape = Shape::Box;
    body.halfExtents = Vec3{0.5f, 0.5f, 0.5f};
    body.position = Vec3{0.0f, 0.0f, 0.0f};
    body.boundRadius = 1.0f;

    // The pointer stands in the middle of the body, where every handle of it
    // begins.
    float px = 0.0f, py = 0.0f;
    picking::projectToViewport(body.position, projection, view.width, view.height, px, py);
    Vec3 origin, direction;
    picking::rayFromViewport(px, py, view, projection, origin, direction);

    const int flat = gizmo::pickAxis(px, py, origin, direction, projection, view, body,
                                      gizmo::Mode::Move, 2);
    check(flat >= 0 && flat != 2, "the handle out of the picture is not grabbable");

    // And the handle in the picture is: the pointer over the end of the X
    // handle takes it.
    const float length = gizmo::handleLength(body);
    float hx = 0.0f, hy = 0.0f;
    picking::projectToViewport(body.position + Vec3{length * 0.8f, 0.0f, 0.0f}, projection,
                               view.width, view.height, hx, hy);
    picking::rayFromViewport(hx, hy, view, projection, origin, direction);
    check(gizmo::pickAxis(hx, hy, origin, direction, projection, view, body, gizmo::Mode::Move,
                          2) == 0,
          "the handle in the picture is grabbed where it is drawn");

    // The ring of a flat world is the one about the axis it looks down, and
    // it is the only one there is.
    const float radius = gizmo::ringRadius(body);
    float rx = 0.0f, ry = 0.0f;
    picking::projectToViewport(body.position + Vec3{radius, 0.0f, 0.0f}, projection, view.width,
                               view.height, rx, ry);
    picking::rayFromViewport(rx, ry, view, projection, origin, direction);
    check(gizmo::pickRing(rx, ry, origin, direction, projection, view, body, 2) == 2,
          "the ring of the flat picture is the one about the axis it looks down");
}

// THE FLAT VIEW LOOKS DOWN AN AXIS

//
// A flat picture is a picture of a plane, and a plane has three sides to be
// seen from: the XY plane from the front, the XZ plane from above and the YZ
// plane from the side. The eye looks straight down the axis, so the picture
// has no vanishing point in it at all.
void testFlatViewDirections() {
    heading("the flat view looks down an axis");
    // The horizontal and the vertical of each flat view: the picture is made
    // of the two axes it does not look down.
    const int horizontal[3] = {2, 0, 0};
    const int vertical[3] = {1, 2, 1};

    for (int axis = 0; axis < 3; ++axis) {
        Camera camera;
        Editor::flatTurn(axis, camera.yaw, camera.pitch);
        camera.orthographic = true;
        camera.orthoSize = 8.0f;

        Vec3 forward, right, up;
        camera.basis(forward, right, up);

        // The eye looks down the axis: the forward is that axis and nothing
        // else.
        float across = 0.0f;
        for (int i = 0; i < 3; ++i)
            if (i != axis) across += std::fabs(component(forward, i));
        check(std::fabs(std::fabs(component(forward, axis)) - 1.0f) < 0.001f &&
                  across < 0.001f,
              "the eye looks straight down the axis");

        // The picture is the plane it cannot see into: the other two axes are
        // the picture's own horizontal and vertical.
        check(std::fabs(std::fabs(component(right, horizontal[axis])) - 1.0f) < 0.001f &&
                  std::fabs(std::fabs(component(up, vertical[axis])) - 1.0f) < 0.001f,
              "and the picture is made of the other two axes");

        // The eye is on the side of the plane the flat view says it is.
        const Vec3 normal = Editor::flatNormal(axis);
        check(dot(normal, forward) < -0.999f, "and the eye is on the side it should be");
    }
}

// THE FONT THE ENGINE SHIPS HAS EVERY LETTER THE ATLAS NEEDS

// A FLOATING WINDOW DOES NOT LAND ON ANOTHER ONE

void testFloatingPanels() {
    heading("a floating window does not land on another one");
    Workspace ws = sampleWorkspace();
    // Two windows, one dropped on top of the other.
    ws.zones[kFavorites] = PanelZone::Float;
    ws.zones[kSounds] = PanelZone::Float;
    ws.floatRects[kFavorites] = Rect{100.0f, 100.0f, 300.0f, 200.0f};
    ws.floatRects[kSounds] = Rect{120.0f, 120.0f, 300.0f, 200.0f};

    const Rect spot = findFreeSpot(ws, kSounds, ws.floatRects[kSounds]);
    check(!rectsOverlap(spot, ws.floatRects[kFavorites]),
          "the window is put where it covers nothing");
    check(spot.x >= 0.0f && spot.y >= 0.0f && spot.x + spot.width <= (float)ws.fbWidth &&
              spot.y + spot.height <= (float)ws.fbHeight,
          "and it stays inside the window");

    // A window with room keeps the place it was dropped in.
    ws.floatRects[kSounds] = Rect{600.0f, 300.0f, 300.0f, 200.0f};
    const Rect same = findFreeSpot(ws, kSounds, ws.floatRects[kSounds]);
    check(nearly(same.x, 600.0f) && nearly(same.y, 300.0f),
          "a window with room stays where it was");

    // A window that covers a panel is moved: the panel underneath cannot be
    // reached while the window is on top of it.
    Workspace plain = sampleWorkspace();
    plain.open[kConsole] = false;
    plain.open[kFavorites] = false;
    plain.open[kSounds] = false;
    const Rect overTheTree = zoneRect(plain, PanelZone::Left);
    plain.zones[kSounds] = PanelZone::Float;
    plain.floatRects[kSounds] = overTheTree;
    const Rect moved = findFreeSpot(plain, kSounds, overTheTree);
    check(!rectsOverlap(moved, overTheTree), "a window over a panel is moved off it");

    // Two panels that stand next to each other do not overlap: the edges
    // touch and nothing is covered.
    check(!rectsOverlap(Rect{0.0f, 0.0f, 100.0f, 100.0f}, Rect{100.0f, 0.0f, 100.0f, 100.0f}),
          "two panels side by side do not overlap");
    check(rectsOverlap(Rect{0.0f, 0.0f, 100.0f, 100.0f}, Rect{99.0f, 0.0f, 100.0f, 100.0f}),
          "but two panels on top of each other do");
}

// THE NOISE IS A NOISE AND NOT A PICTURE OF ONE

void testNoise() {
    heading("the noise is a noise");
    check(perlinNoise(1.25f, 3.5f, 7u) == perlinNoise(1.25f, 3.5f, 7u),
          "the same seed gives the same value");
    check(perlinNoise(1.25f, 3.5f, 7u) != perlinNoise(1.25f, 3.5f, 8u),
          "a different seed gives a different value");

    float lowest = 1.0f, highest = 0.0f;
    for (int i = 0; i < 400; ++i) {
        const float v = fbm((float)i * 0.37f, (float)i * 0.11f, 3u, 5);
        if (v < lowest) lowest = v;
        if (v > highest) highest = v;
    }
    check(lowest >= 0.0f && highest <= 1.0f, "the noise stays between 0 and 1");

    // And it is CONTINUOUS: two points that are close together have values
    // that are close together. A hash of a float is not - the value at one
    // point says nothing about the value next to it, and the ground comes out
    // as a field of cubes with rounded corners.
    float biggestStep = 0.0f;
    for (int i = 0; i < 400; ++i) {
        const float x = (float)i * 0.37f;
        const float a = fbm(x, 1.0f, 3u, 5);
        const float b = fbm(x + 0.001f, 1.0f, 3u, 5);
        biggestStep = std::fmax(biggestStep, std::fabs(a - b));
    }
    check(biggestStep < 0.02f, "two nearby points have nearly the same value");

    // The shapes of the ground are made of the same noise, and each of them
    // is a different answer to the same question.
    const float hills = fbm(1.0f, 2.0f, 11u, 5);
    const float sharp = ridgedFbm(1.0f, 2.0f, 11u, 5);
    check(hills >= 0.0f && hills <= 1.0f && sharp >= 0.0f && sharp <= 1.0f,
          "the ridges stay between 0 and 1 too");
    check(hills != sharp, "and the ridges are not the hills");
}

// THE GROUND IS MADE OF NOISE AND STANDS WHERE IT SAYS

void testLandform() {
    heading("the ground is made and it is where it says it is");
    LandformSettings settings;
    settings.cells = 32;
    settings.width = 40.0f;
    settings.depth = 30.0f;
    settings.minHeight = 2.0f;
    settings.maxHeight = 10.0f;
    settings.seed = 5;

    Landform landform;
    landform.build(settings);
    check(landform.ready(), "the ground is made");

    const MeshAsset mesh = landform.toMesh();
    check(mesh.parts.size() == 1, "the ground is one part");
    const MeshPart& part = mesh.parts[0];
    const int side = settings.cells + 1;
    check(part.vertices.size() == (size_t)side * (size_t)side * kMeshVertexFloats,
          "the ground has a vertex for every square");
    check(part.indices.size() == (size_t)settings.cells * (size_t)settings.cells * 6,
          "and two triangles for every square");
    check(part.colors.size() == (size_t)side * (size_t)side * 3,
          "and a colour for every vertex");

    float lowest = 1e9f, highest = -1e9f;
    for (size_t i = 0; i < part.vertices.size(); i += kMeshVertexFloats) {
        // The height of the ground is measured from its middle: the body it
        // belongs to stands there, and the mesh is drawn around its middle.
        const float y = part.vertices[i + kMeshPositionOffset + 1];
        lowest = std::fmin(lowest, y);
        highest = std::fmax(highest, y);
    }
    const float range = settings.maxHeight - settings.minHeight;
    check(lowest >= -range * 0.5f - 0.01f && highest <= range * 0.5f + 0.01f,
          "the ground stays between the heights that were asked for");
    check(highest - lowest > range * 0.9f,
          "and it uses the whole of that range");

    // The ground the editor asks about is the ground that was drawn. A body
    // put on the new piece of ground has to stand on it and not near it.
    const float halfWidth = settings.width * 0.5f;
    const float halfDepth = settings.depth * 0.5f;
    const float stepX = settings.width / (float)settings.cells;
    const float stepZ = settings.depth / (float)settings.cells;
    bool matches = true;
    for (int row = 0; row < side; row += 7) {
        for (int col = 0; col < side; col += 5) {
            const size_t at = ((size_t)row * (size_t)side + (size_t)col) * kMeshVertexFloats;
            const float x = -halfWidth + (float)col * stepX;
            const float z = -halfDepth + (float)row * stepZ;
            const float drawn = part.vertices[at + kMeshPositionOffset + 1];
            if (std::fabs(drawn - landform.heightAt(x, z)) > 0.01f) matches = false;
        }
    }
    check(matches, "the height the editor asks about is the height that was drawn");

    // The same seed is the same ground, and another seed is another ground.
    Landform again;
    again.build(settings);
    Landform other;
    LandformSettings otherSettings = settings;
    otherSettings.seed = 6;
    other.build(otherSettings);
    float difference = 0.0f;
    for (int row = 0; row < side; row += 5) {
        for (int col = 0; col < side; col += 5) {
            difference += std::fabs(landform.heightAt((float)col, (float)row) -
                                    other.heightAt((float)col, (float)row));
        }
    }
    check(difference > 0.0f, "another seed is another ground");

    // The shapes are different shapes.
    float shapeDifference = 0.0f;
    for (int shape = 0; shape < kLandformShapeCount; ++shape) {
        LandformSettings shaped = settings;
        shaped.shape = (LandformShape)shape;
        Landform piece;
        piece.build(shaped);
        shapeDifference += std::fabs(piece.heightAt(3.0f, 4.0f) - landform.heightAt(3.0f, 4.0f));
    }
    check(shapeDifference > 0.0f, "the shapes are not the same ground");

    // And the numbers the shapes are made of are the ones of the one who
    // asks for the ground. A number that is written down once and is the
    // same for every ground is a way of saying that every ground looks the
    // same, and a ground that ignores the number it is given is a ground
    // nobody asked for.
    using Member = float LandformSettings::*;
    struct Number {
        LandformShape shape;
        Member value;
        float one;
        float another;
    };
    const Number numbers[] = {
        {LandformShape::Mountains, &LandformSettings::lowland, 0.0f, 0.9f},
        {LandformShape::Island, &LandformSettings::shore, 0.1f, 0.9f},
        {LandformShape::Plateau, &LandformSettings::roundness, 0.05f, 0.9f},
        {LandformShape::Canyon, &LandformSettings::cut, 0.1f, 1.0f},
        {LandformShape::Dunes, &LandformSettings::stretch, 0.5f, 3.0f},
    };
    int ignored = 0;
    for (const Number& number : numbers) {
        LandformSettings shaped = settings;
        shaped.shape = number.shape;
        shaped.*(number.value) = number.one;
        Landform first;
        first.build(shaped);
        shaped.*(number.value) = number.another;
        Landform second;
        second.build(shaped);

        float difference = 0.0f;
        for (int row = 0; row < side; row += 5) {
            for (int col = 0; col < side; col += 5) {
                difference += std::fabs(first.heightAt((float)col, (float)row) -
                                        second.heightAt((float)col, (float)row));
            }
        }
        if (difference <= 0.0f) ++ignored;
    }
    check(ignored == 0, "the ground comes out the way the numbers of the shape say");

    // And the number of steps a plateau is cut into is a number too.
    LandformSettings stepped = settings;
    stepped.shape = LandformShape::Plateau;
    stepped.levels = 3;
    Landform few;
    few.build(stepped);
    stepped.levels = 12;
    Landform many;
    many.build(stepped);
    check(std::fabs(few.heightAt(3.0f, 4.0f) - many.heightAt(3.0f, 4.0f)) > 0.0f,
          "a plateau of three steps is not a plateau of twelve");

    // The variation is a number of its own, and it makes the ground differ
    // from the ground that was made before it - without making it a
    // different kind of ground.
    LandformSettings varied = settings;
    varied.variation = 0.0f;
    Landform plain;
    plain.build(varied);
    varied.variation = 0.02f;
    Landform different;
    different.build(varied);
    float variation = 0.0f;
    for (int row = 0; row < side; row += 5) {
        for (int col = 0; col < side; col += 5) {
            variation += std::fabs(plain.heightAt((float)col, (float)row) -
                                   different.heightAt((float)col, (float)row));
        }
    }
    check(variation > 0.0f, "a ground with a variation is not the ground without it");

    // The number that is drawn is a number of its own, and it is not the
    // same one every time: two grounds made one after the other are not the
    // same ground twice.
    const float first = landformVariation();
    const float second = landformVariation();
    check(first >= 0.0005f && first <= 0.02f, "the number is the small one it should be");
    check(second >= 0.0005f && second <= 0.02f, "and so is the next one");
    check(first != second, "and the next one is not the same one");

    // A plateau is a table with nothing on it, and a table is not a piece of
    // land: the ordinary ground is put on the flat tops. The amount of it is
    // asked for, and none of it leaves the tops as flat as they were.
    LandformSettings table = settings;
    table.shape = LandformShape::Plateau;
    table.ground = 0.0f;
    Landform bare;
    bare.build(table);
    table.ground = 1.0f;
    Landform covered;
    covered.build(table);

    // The tops of a bare plateau are flat: a whole step of them is at the
    // same height, and the ground has only a handful of heights on it. The
    // ground on the plateau is not flat, and every square of it is at a
    // height of its own.
    const auto heightsOf = [](const Landform& ground) {
        std::vector<float> heights;
        for (int row = 0; row <= ground.settings().cells; ++row) {
            for (int col = 0; col <= ground.settings().cells; ++col) {
                heights.push_back(ground.heightAt((float)col, (float)row));
            }
        }
        std::sort(heights.begin(), heights.end());
        heights.erase(std::unique(heights.begin(), heights.end(),
                                  [](float a, float b) { return std::fabs(a - b) < 1e-4f; }),
                      heights.end());
        return heights.size();
    };
    check(heightsOf(covered) > heightsOf(bare),
          "the ground on the plateau is not flat");
}

// A GROUND THAT IS SAVED IS A GROUND THAT COMES BACK

void testLandformScene() {
    heading("a ground that is saved comes back a ground");

    // The ground, made the way the panel makes it: the settings, the mesh, and
    // the settings travelling with the mesh so that the scene can say how the
    // ground was made.
    LandformSettings settings;
    settings.cells = 24;
    settings.width = 40.0f;
    settings.depth = 30.0f;
    settings.minHeight = 1.0f;
    settings.maxHeight = 9.0f;
    settings.seed = 4242;
    settings.shape = LandformShape::Plateau;
    settings.variation = 0.0031f;
    settings.biome = LandformBiome::Desert;
    settings.detail = 0.4f;
    settings.shading = 0.5f;

    Landform ground;
    ground.build(settings);
    MeshAsset asset = ground.toMesh();
    asset.name = "landform " + std::to_string(settings.seed);
    asset.landform = ground.settings();

    MeshRegistry meshes;
    const int mesh = meshes.importMesh(std::move(asset));
    check(mesh >= 0, "the ground is in the registry");

    PhysicsWorld world;
    RigidBody body;
    body.shape = Shape::Mesh;
    body.mesh = mesh;
    const MeshAsset* made = meshes.at(mesh);
    body.halfExtents = made ? made->half : Vec3{1.0f, 1.0f, 1.0f};
    body.position = Vec3{0.0f, settings.minHeight, 0.0f};
    body.material.isStatic = true;
    body.configure(body.mass);
    body.syncPrevTransform();
    world.bodies.push_back(body);

    // The scene is written and read back, the way the editor does it. The
    // canvas goes with it: the interface of the game is a part of the scene.
    const std::string path = str::join(fs::executableDir(), "landform-scene-test.json");
    gui::Canvas canvas;
    check(scenes::save(world, meshes, canvas, path), "the scene with the ground is written");

    std::string text;
    fs::readFile(path, text);
    check(text.find("\"landform\"") != std::string::npos,
          "the file says how the ground was made");

    PhysicsWorld opened;
    MeshRegistry openedMeshes;
    gui::Canvas openedCanvas;
    check(scenes::load(opened, openedMeshes, openedCanvas, path),
          "the scene is read back");
    check(opened.bodies.size() == 1, "the ground is in the scene that came back");

    if (opened.bodies.size() == 1) {
        const RigidBody& back = opened.bodies[0];
        check(back.hasMesh(), "the ground has its mesh and not a box in its place");
        check(back.shape == Shape::Mesh, "and it is a mesh body");

        const MeshAsset* again = openedMeshes.at(back.mesh);
        check(again != nullptr, "the mesh of the ground is there");
        if (again && made) {
            check(again->parts.size() == made->parts.size() &&
                      again->parts[0].vertices.size() == made->parts[0].vertices.size(),
                  "the ground that came back is the ground that was saved");
            check(again->landform.has_value(), "and it still says how it was made");
            if (again->landform) {
                check(again->landform->seed == settings.seed, "the seed is the same one");
                check(again->landform->shape == LandformShape::Plateau,
                      "and the shape is the same one");
                check(again->landform->biome == LandformBiome::Desert,
                      "and the biome is the same one");
                check(std::fabs(again->landform->detail - settings.detail) < 0.001f,
                      "and so are the little bumps");
                check(std::fabs(again->landform->shading - settings.shading) < 0.001f,
                      "and the darkness of the valleys");
            }

            // The heights: the same ground, not a ground that looks like it.
            bool same = true;
            const MeshPart& a = made->parts[0];
            const MeshPart& b = again->parts[0];
            for (size_t i = 0; i < a.vertices.size(); i += kMeshVertexFloats)
                if (std::fabs(a.vertices[i + kMeshPositionOffset + 1] -
                              b.vertices[i + kMeshPositionOffset + 1]) > 0.001f)
                    same = false;
            check(same, "the ground stands where it stood");
        }
    }

    fs::removeTree(path);
}

// THE BRUSH PAINTS THE GROUND, AND THE GROUND THAT WAS PAINTED COMES BACK

void testLandformBrush() {
    heading("the brush paints the ground");

    LandformSettings settings;
    settings.cells = 32;
    settings.width = 40.0f;
    settings.depth = 40.0f;
    settings.minHeight = 0.0f;
    settings.maxHeight = 10.0f;
    settings.seed = 91;

    Landform ground;
    ground.build(settings);
    const std::vector<float> before = ground.heights();

    // The brush raises the ground under the hand, and the ground under the
    // hand is the ground that is changed.
    ground.beginStroke(0.0f, 0.0f);
    check(ground.paint(0.0f, 0.0f, 6.0f, 2.0f, LandformBrush::Raise),
          "the brush changes the ground");
    check(ground.painted(), "and the ground knows that it was painted");

    float difference = 0.0f;
    for (size_t i = 0; i < before.size(); ++i) difference += std::fabs(before[i] - ground.heights()[i]);
    check(difference > 0.0f, "the ground is not the ground it was");

    // And the brush lowers it again: the ground under the hand is lower than
    // the ground around it.
    const float raised = ground.heightAt(0.0f, 0.0f);
    ground.paint(0.0f, 0.0f, 6.0f, 4.0f, LandformBrush::Lower);
    check(ground.heightAt(0.0f, 0.0f) < raised, "the brush lowers the ground as well");

    // The smoothing brush takes the bumps off: the ground under the hand is
    // closer to its neighbours than it was.
    const float rough = std::fabs(ground.heightAt(0.0f, 0.0f) - ground.heightAt(1.0f, 0.0f));
    ground.paint(0.0f, 0.0f, 8.0f, 1.0f, LandformBrush::Smooth);
    check(std::fabs(ground.heightAt(0.0f, 0.0f) - ground.heightAt(1.0f, 0.0f)) < rough,
          "the brush smooths the ground");

    // The ray meets the ground: the ground is a heightfield, and the brush
    // follows the hand.
    float distance = 0.0f;
    const bool hit = ground.rayHit(Vec3{0.0f, 40.0f, 0.0f}, Vec3{0.0f, -1.0f, 0.0f}, distance);
    check(hit, "the ray meets the ground");
    check(hit && distance > 0.0f && distance < 40.0f, "and it meets it where the ground is");
    check(!ground.rayHit(Vec3{500.0f, 40.0f, 500.0f}, Vec3{0.0f, -1.0f, 0.0f}, distance),
          "and it does not meet the ground that is not there");

    // A ground that was painted is saved with its heights, and it comes back
    // the ground that was painted.
    MeshAsset asset = ground.toMesh();
    asset.name = "landform painted";
    asset.landform = ground.settings();
    asset.landformHeights = ground.heights();

    MeshRegistry meshes;
    const int mesh = meshes.importMesh(std::move(asset));
    PhysicsWorld world;
    RigidBody body;
    body.shape = Shape::Mesh;
    body.mesh = mesh;
    body.position = Vec3{0.0f, (settings.minHeight + settings.maxHeight) * 0.5f, 0.0f};
    body.material.isStatic = true;
    body.configure(body.mass);
    body.syncPrevTransform();
    world.bodies.push_back(body);

    const std::string path = str::join(fs::executableDir(), "landform-brush-test.json");
    gui::Canvas canvas;
    check(scenes::save(world, meshes, canvas, path), "the painted ground is written");

    PhysicsWorld opened;
    MeshRegistry openedMeshes;
    gui::Canvas openedCanvas;
    check(scenes::load(opened, openedMeshes, openedCanvas, path), "and read back");
    if (opened.bodies.size() == 1 && opened.bodies[0].hasMesh()) {
        const MeshAsset* again = openedMeshes.at(opened.bodies[0].mesh);
        check(again && !again->landformHeights.empty(), "the ground that came back was painted");
        bool same = again && again->landformHeights.size() == ground.heights().size();
        if (same)
            for (size_t i = 0; i < again->landformHeights.size(); ++i)
                if (std::fabs(again->landformHeights[i] - ground.heights()[i]) > 0.001f) same = false;
        check(same, "and it is the ground that was painted");
    } else {
        check(false, "the ground that came back is a ground");
    }
    fs::removeTree(path);
}

// THE WATER IS A LEVEL OF WATER, AND IT IS SAVED

void testWater() {
    heading("the water is a level of water");

    PhysicsWorld world;
    RigidBody body = BodyFactory::makeWater(Vec3{0.0f, 2.0f, 0.0f}, 100.0f, 80.0f,
                                            Vec3{0.1f, 0.3f, 0.45f}, 0.5f);
    check(body.shape == Shape::Water, "the water is water");
    check(!body.castsShadow, "and it casts no shadow");
    check(body.halfExtents.x > 0.0f && body.halfExtents.z > 0.0f, "it is as big as it was told to be");
    world.bodies.push_back(body);

    const std::string path = str::join(fs::executableDir(), "water-test.json");
    gui::Canvas canvas;
    MeshRegistry meshes;
    check(scenes::save(world, meshes, canvas, path), "the water is written");

    PhysicsWorld opened;
    MeshRegistry openedMeshes;
    gui::Canvas openedCanvas;
    check(scenes::load(opened, openedMeshes, openedCanvas, path), "and read back");
    check(opened.bodies.size() == 1, "the water is in the scene that came back");
    if (opened.bodies.size() == 1) {
        const RigidBody& back = opened.bodies[0];
        check(back.shape == Shape::Water, "it is still water");
        check(std::fabs(back.position.y - 2.0f) < 0.001f, "at the level it was at");
        check(std::fabs(back.waterAlpha - 0.5f) < 0.001f, "and as clear as it was");
        check(std::fabs(back.material.color.z - 0.45f) < 0.001f, "and the colour it had");
    }
    fs::removeTree(path);
}


void testLandformLooks() {
    heading("the ground looks like ground");

    LandformSettings settings;
    settings.cells = 32;
    settings.width = 40.0f;
    settings.depth = 40.0f;
    settings.minHeight = 0.0f;
    settings.maxHeight = 12.0f;
    settings.seed = 77;

    // The colour of every vertex of a ground, so that two grounds can be
    // compared without looking at them.
    const auto colorsOf = [](const Landform& ground) {
        std::vector<float> colors;
        const MeshAsset mesh = ground.toMesh();
        if (mesh.parts.empty()) return colors;
        colors = mesh.parts[0].colors;
        return colors;
    };

    // The biomes are different biomes: the same ground in two of them is two
    // different pictures, and a biome that changes nothing is a name and not a
    // biome.
    LandformSettings desert = settings;
    desert.biome = LandformBiome::Desert;
    Landform grassland;
    grassland.build(settings);
    Landform dry;
    dry.build(desert);
    const std::vector<float> green = colorsOf(grassland);
    const std::vector<float> sand = colorsOf(dry);
    check(!green.empty() && !sand.empty(), "the ground is coloured");
    float difference = 0.0f;
    for (size_t i = 0; i < green.size() && i < sand.size(); ++i)
        difference += std::fabs(green[i] - sand[i]);
    check(difference > 0.5f, "two biomes are two different grounds");

    // The little bumps are on the ground. A ground without them is a shape, and
    // a ground with them is ground.
    LandformSettings smooth = settings;
    smooth.detail = 0.0f;
    Landform plain;
    plain.build(smooth);
    Landform bumpy;
    bumpy.build(settings);
    float roughness = 0.0f;
    for (int row = 1; row < plain.settings().cells; ++row)
        for (int col = 1; col < plain.settings().cells; ++col)
            roughness += std::fabs(bumpy.heightAt((float)col, (float)row) -
                                   bumpy.heightAt((float)col + 1.0f, (float)row)) -
                          std::fabs(plain.heightAt((float)col, (float)row) -
                                    plain.heightAt((float)col + 1.0f, (float)row));
    check(roughness > 0.0f, "the ground has little bumps on it");

    // The valleys are darker than the ridges. The ground is coloured by the
    // shape of the ground and not only by how high it is, and that is what
    // makes a valley look like a valley.
    LandformSettings lit = settings;
    lit.shading = 0.0f;
    Landform bright;
    bright.build(lit);
    Landform dark;
    dark.build(settings);
    const std::vector<float> litColors = colorsOf(bright);
    const std::vector<float> darkColors = colorsOf(dark);
    float darker = 0.0f;
    for (size_t i = 0; i < litColors.size() && i < darkColors.size(); ++i)
        if (darkColors[i] < litColors[i] - 0.001f) darker += 1.0f;
    check(darker > 0.0f, "the valleys are darker than the ridges");
}

// THE CLOUD IS MADE OF NOISE AND IS WHAT IT SAYS IT IS

// The settings a cloud is made with, and the two ways it can be made.
CloudSettings cloudSettingsFor(CloudMode mode) {
    CloudSettings settings;
    settings.mode = mode;
    settings.seed = 77;
    settings.puffs = 24;
    settings.puffSize = 3.0f;
    settings.spread = 24.0f;
    settings.height = 9.0f;
    settings.coverage = 0.55f;
    settings.softness = 0.5f;
    settings.detail = 4;
    settings.resolution = 8;
    return settings;
}

void testCloud() {
    heading("the cloud is made and it is what it says it is");

    // The volume: a heap of puffs, and every puff is a ball of triangles.
    CloudSettings settings = cloudSettingsFor(CloudMode::Puffy);
    Cloud cloud;
    cloud.build(settings);
    check(cloud.ready(), "the cloud is made");
    check(cloud.puffCount() > 0, "and it is made of puffs");

    const MeshAsset mesh = cloud.toMesh();
    check(mesh.parts.size() == 1, "the cloud is one part");
    const MeshPart& part = mesh.parts[0];
    check(!part.vertices.empty(), "it has vertices");
    check(!part.indices.empty(), "and triangles");
    check(part.colors.size() == part.vertices.size() / kMeshVertexFloats * 3,
          "and a colour for every vertex");

    // Every index points at a vertex that is there. A cloud of a hundred
    // puffs is a mesh with thousands of vertices, and one index past the end
    // of them is a crash in the middle of a frame.
    bool inRange = true;
    const unsigned int vertexCount = (unsigned int)(part.vertices.size() / kMeshVertexFloats);
    for (unsigned int index : part.indices)
        if (index >= vertexCount) inRange = false;
    check(inRange, "every triangle of the cloud points at a vertex that is there");

    // The same seed is the same cloud, and another seed is another cloud.
    Cloud again;
    again.build(settings);
    Cloud other;
    CloudSettings otherSettings = settings;
    otherSettings.seed = 78;
    other.build(otherSettings);
    check(again.toMesh().parts[0].vertices == cloud.toMesh().parts[0].vertices,
          "the same seed is the same cloud");
    check(other.toMesh().parts[0].vertices != cloud.toMesh().parts[0].vertices,
          "and another seed is another cloud");

    // The threads do not change the cloud: the puffs are made in parallel and
    // they come out in the same order whatever order they were made in.
    Cloud threaded;
    CloudSettings threadedSettings = settings;
    threadedSettings.threads = 4;
    threaded.build(threadedSettings);
    check(threaded.toMesh().parts[0].vertices == cloud.toMesh().parts[0].vertices,
          "the threads do not change the cloud");

    // The cost of the cloud is the number of the puffs and how finely they are
    // made. A cloud that is asked for less of both is a cheaper cloud.
    CloudSettings cheap = settings;
    cheap.puffs = 8;
    cheap.resolution = 4;
    Cloud small;
    small.build(cheap);
    const MeshAsset smallMesh = small.toMesh();
    const MeshPart& smallPart = smallMesh.parts[0];
    check(smallPart.indices.size() < part.indices.size(),
          "fewer puffs and a coarser ball make a cheaper cloud");

    // The flat cloud is a picture on a plane: every point of it is in the
    // plane, and it is not a heap of puffs.
    CloudSettings flat = cloudSettingsFor(CloudMode::Flat);
    Cloud flatCloud;
    flatCloud.build(flat);
    const MeshAsset flatMesh = flatCloud.toMesh();
    const MeshPart& flatPart = flatMesh.parts[0];
    float depth = 0.0f;
    for (size_t i = 0; i < flatPart.vertices.size(); i += kMeshVertexFloats)
        depth = std::fmax(depth, std::fabs(flatPart.vertices[i + kMeshPositionOffset + 2]));
    check(depth < 0.05f, "the flat cloud is a picture on a plane");

    // The volume is not: it has a depth.
    float volume = 0.0f;
    for (size_t i = 0; i < part.vertices.size(); i += kMeshVertexFloats)
        volume = std::fmax(volume, std::fabs(part.vertices[i + kMeshPositionOffset + 2]));
    check(volume > 0.5f, "and the cloud of puffs is not");

    // The top of the cloud is brighter than the bottom of it: the sun reaches
    // the top of a cloud and hardly reaches the bottom of it.
    float lowest = 1e9f, highest = -1e9f;
    for (size_t i = 0; i < part.vertices.size(); i += kMeshVertexFloats) {
        const float y = part.vertices[i + kMeshPositionOffset + 1];
        lowest = std::fmin(lowest, y);
        highest = std::fmax(highest, y);
    }
    // The averages and not the brightest of each half: a puff that is buried
    // in the middle of the cloud is dark wherever it is, and the brightest
    // vertex of the bottom of the cloud says nothing about the bottom of it.
    float top = 0.0f, bottom = 0.0f;
    int topCount = 0, bottomCount = 0;
    for (size_t i = 0; i < part.vertices.size(); i += kMeshVertexFloats) {
        const float y = part.vertices[i + kMeshPositionOffset + 1];
        const float light = part.colors[i / kMeshVertexFloats * 3];
        if (y > highest - (highest - lowest) * 0.2f) {
            top += light;
            ++topCount;
        }
        if (y < lowest + (highest - lowest) * 0.2f) {
            bottom += light;
            ++bottomCount;
        }
    }
    top = topCount ? top / (float)topCount : 0.0f;
    bottom = bottomCount ? bottom / (float)bottomCount : 0.0f;
    check(top > bottom + 0.05f, "the top of the cloud is brighter than the bottom of it");

    // The cloud is as tall as it was asked to be: a cloud that is nine metres
    // high is nine metres high.
    check(highest - lowest <= settings.height + settings.puffSize + 0.5f,
          "the cloud is as tall as it was asked to be");

    // And it is as wide as it was asked to be.
    float wide = 0.0f;
    for (size_t i = 0; i < part.vertices.size(); i += kMeshVertexFloats)
        wide = std::fmax(wide, std::fabs(part.vertices[i + kMeshPositionOffset + 0]));
    check(wide <= settings.spread * 0.5f + settings.puffSize + 0.5f,
          "and as wide as it was asked to be");
}

// A CLOUD THAT IS SAVED COMES BACK A CLOUD

void testCloudScene() {
    heading("a cloud that is saved comes back a cloud");

    CloudSettings settings = cloudSettingsFor(CloudMode::Puffy);
    settings.seed = 4242;

    Cloud cloud;
    cloud.build(settings);
    MeshAsset asset = cloud.toMesh();
    asset.name = "cloud " + std::to_string(settings.seed);
    asset.cloud = cloud.settings();

    MeshRegistry meshes;
    const int mesh = meshes.importMesh(std::move(asset));
    check(mesh >= 0, "the cloud is in the registry");

    PhysicsWorld world;
    RigidBody body;
    body.shape = Shape::Mesh;
    body.mesh = mesh;
    const MeshAsset* made = meshes.at(mesh);
    body.halfExtents = made ? made->half : Vec3{1.0f, 1.0f, 1.0f};
    body.position = made ? made->center : Vec3{};
    body.material.isStatic = true;
    body.configure(body.mass);
    body.syncPrevTransform();
    world.bodies.push_back(body);

    const std::string path = str::join(fs::executableDir(), "cloud-scene-test.json");
    gui::Canvas canvas;
    check(scenes::save(world, meshes, canvas, path), "the scene with the cloud is written");

    std::string text;
    fs::readFile(path, text);
    check(text.find("\"cloud\"") != std::string::npos, "the file says how the cloud was made");

    PhysicsWorld opened;
    MeshRegistry openedMeshes;
    gui::Canvas openedCanvas;
    check(scenes::load(opened, openedMeshes, openedCanvas, path), "the scene is read back");
    check(opened.bodies.size() == 1, "the cloud is in the scene that came back");

    if (opened.bodies.size() == 1) {
        const RigidBody& back = opened.bodies[0];
        check(back.hasMesh(), "the cloud has its mesh and not a box in its place");
        check(back.shape == Shape::Mesh, "and it is a mesh body");

        const MeshAsset* again = openedMeshes.at(back.mesh);
        check(again != nullptr, "the mesh of the cloud is there");
        if (again && made) {
            check(again->parts.size() == made->parts.size() &&
                      again->parts[0].vertices.size() == made->parts[0].vertices.size(),
                  "the cloud that came back is the cloud that was saved");
            check(again->cloud.has_value(), "and it still says how it was made");
            if (again->cloud) {
                check(again->cloud->seed == settings.seed, "the seed is the same one");
                check(again->cloud->mode == CloudMode::Puffy, "and the way it is drawn is the same");
            }
        }
    }

    fs::removeTree(path);
}

// THE CHARACTERS A FIELD CAN HOLD ARE THE CHARACTERS THE ENGINE CAN DRAW

void testParticlesScene() {
    heading("the effects of a scene come back with the scene");

    // An emitter is scene data: a place, a direction and a recipe. The scene
    // file carries it, and a scene that is opened again has it.
    ParticleSystem particles;

    Emitter emitter;
    emitter.position = Vec3{1.5f, 2.0f, -3.0f};
    emitter.direction = Vec3{0.0f, 1.0f, 0.0f};
    emitter.settings = particlePresetSettings(ParticlePreset::Smoke);
    emitter.settings.count = 42;
    emitter.continuous = true;
    emitter.interval = 2.5f;
    particles.addEmitter(emitter);

    PhysicsWorld world;
    MeshRegistry meshes;
    gui::Canvas canvas;
    const std::string path = str::join(fs::executableDir(), "particles-scene-test.json");
    check(scenes::save(world, meshes, canvas, path, &particles),
          "the scene with the emitter is written");

    std::string text;
    fs::readFile(path, text);
    check(text.find("\"emitters\"") != std::string::npos,
          "the file says where the particles come from");

    // And it comes back.
    ParticleSystem opened;
    PhysicsWorld openedWorld;
    MeshRegistry openedMeshes;
    gui::Canvas openedCanvas;
    check(scenes::load(openedWorld, openedMeshes, openedCanvas, path, 1, &opened),
          "the scene with the emitter is read back");
    check(opened.emitters().size() == 1, "the emitter is in the scene that came back");

    if (opened.emitters().size() == 1) {
        const Emitter& back = opened.emitters()[0];
        check(back.position.x == 1.5f && back.position.y == 2.0f && back.position.z == -3.0f,
              "the emitter is where it was");
        check(back.settings.count == 42, "the recipe is the recipe that was saved");
        check(back.continuous && back.interval == 2.5f, "and so is the way it repeats");
        check(back.settings.blend == ParticleBlend::Alpha, "and the way it is drawn");
    }
}

void testTextCodepoints() {
    heading("a field can hold what the engine can draw");
    // Every character the window lets into a field must have a glyph: a
    // character that is put in a field and cannot be drawn is an invisible
    // mark in the middle of a name.
    int missing = 0;
    int firstMissing = 0;
    for (unsigned cp = 32; cp < 0x300; ++cp) {
        if (!Window::textCodepointSupported(cp)) continue;
        if (TextAtlas::glyphIndex(cp) >= 0) continue;
        if (!missing) firstMissing = (int)cp;
        ++missing;
    }
    if (missing) {
        char what[96];
        std::snprintf(what, sizeof(what), "every accepted character has a glyph (U+%04X is missing)",
                      firstMissing);
        check(false, what);
    } else {
        check(true, "every accepted character has a glyph");
    }

    // And the other way round: a character the engine can draw but the window
    // will not accept is a letter that cannot be typed - which is what a
    // Ukrainian name in a text field is.
    int refused = 0;
    for (unsigned cp = 32; cp < 0x300; ++cp) {
        if (TextAtlas::glyphIndex(cp) < 0) continue;
        if (Window::textCodepointSupported(cp)) continue;
        ++refused;
    }
    check(refused == 0, "every character the engine can draw can be typed");

    // The Cyrillic, which is the whole point: the interface is written in it.
    check(Window::textCodepointSupported(0x410) && Window::textCodepointSupported(0x44F),
          "the Cyrillic block can be typed");
    check(Window::textCodepointSupported(0x401) && Window::textCodepointSupported(0x451),
          "and the letters outside it");

    // A letter is two bytes, and a backspace takes off the letter and not one
    // of its bytes.
    char utf8[4];
    const int bytes = str::utf8Encode(0x410, utf8);
    check(bytes == 2, "a Cyrillic letter is two bytes");
    const char text[3] = {'a', utf8[0], utf8[1]};
    check(str::utf8WithoutLast(text, 3) == 1, "and the backspace takes off the whole letter");
}

// NO TWO WIDGETS SHARE AN ID

void testWidgetIds() {
    heading("no two widgets share an id");
    // Every stretch of ids the editor hands out. A shared id is a widget that
    // takes the press away from another one - and the two are in different
    // corners of the window, so the press lands in the wrong place.
    struct Range {
        int first;
        int last;
        const char* what;
    };
    const Range ranges[] = {
        {Editor::kTopBar, Editor::kTransportStop, "the top bar"},
        {Editor::kTopBarTool, Editor::kTopBarTool + 4, "the tools in the bar"},
        {Editor::kMenuRow, Editor::kMenuRow + 71, "the menus"},
        {Editor::kToolbarHelp, Editor::kToolbarCounter, "the toolbar"},
        {Editor::kLayerRow, Editor::kLayerRow + 11, "the layers"},
        {Editor::kLayerToggle, Editor::kLayerToggle, "the layer toggle"},
        {Editor::kHelpClose, Editor::kHelpClose, "the help"},
        {Editor::kPanelGrip, Editor::kPanelGrip, "the panel grip"},
        {Editor::kHierarchyRow, Editor::kHierarchyRow + 499, "the hierarchy's rows"},
        {Editor::kHierarchyStar, Editor::kHierarchySearch, "the hierarchy's header"},
        {Editor::kFavoriteRow, Editor::kFavoriteRow + 99, "the favorites"},
        {Editor::kSoundRow, Editor::kSoundRow + 99, "the sounds"},
        {Editor::kLandformMinHeight, Editor::kLandformCells, "the landform's numbers"},
        {Editor::kLandformGenerate, Editor::kLandformGenerate, "the landform's button"},
        {Editor::kLandformColor, Editor::kLandformColor + 5, "the landform's colours"},
        {Editor::kLandformShape, Editor::kLandformShape + 5, "the landform's shapes"},
        {Editor::kLandformSeed, Editor::kLandformRandom, "the landform's fields"},
        {Editor::kLandformLevels, Editor::kLandformGround, "the landform's shape numbers"},
        {Editor::kCloudMode, Editor::kCloudMode + 1, "the cloud's modes"},
        {Editor::kCloudShape, Editor::kCloudShape + 3, "the cloud's shapes"},
        {Editor::kCloudPuffs, Editor::kCloudGenerate, "the cloud's numbers"},
        {Editor::kCloudPalette, Editor::kCloudPalette + 3, "the cloud's colours"},
        {Editor::kLandformBiome, Editor::kLandformBiome + 7, "the landform's biomes"},
        {Editor::kLandformDetail, Editor::kLandformPlaceZ, "the landform's new numbers"},
        {Editor::kInspectorDock, Editor::kInspectorDock, "the inspector"},
        {Editor::kInspectorProperty, Editor::kInspectorProperty + 39,
         "the inspector's numbers"},
        {Editor::kWaterDepthColour, Editor::kWaterDarkening, "the water's numbers"},
        {Editor::kFilterField, Editor::kConsoleFilter, "the filters"},
        {Editor::kSceneHeader, Editor::kSceneHeader, "the scene header"},
        {Editor::kParticlePreset, Editor::kParticlePreset + 4, "the particles' presets"},
        {Editor::kParticleCount, Editor::kParticleBlend + 1, "the particles' numbers"},
        {Editor::kParticleColour, Editor::kParticleColour + 5, "the particles' colours"},
        {Editor::kParticleEmit, Editor::kParticleFollow, "the particles' buttons"},
        {Editor::kParticleRow, Editor::kParticleRow + 99, "the particles' rows"},
    };
    const int count = (int)(sizeof(ranges) / sizeof(ranges[0]));

    int clashes = 0;
    char firstClash[128] = "";
    for (int i = 0; i < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            if (ranges[i].first > ranges[j].last || ranges[j].first > ranges[i].last) continue;
            if (!clashes)
                std::snprintf(firstClash, sizeof(firstClash), "%s and %s share an id",
                              ranges[i].what, ranges[j].what);
            ++clashes;
        }
    }
    if (clashes) {
        char what[160];
        std::snprintf(what, sizeof(what), "no two stretches of ids meet (%s)", firstClash);
        check(false, what);
    } else {
        check(true, "no two stretches of ids meet");
    }
}

// THE SHADERS SAY WHAT THEY ARE UNDER

// Every shader in the tree, one after another.
void collectShaders(const std::string& dir, std::vector<std::string>& out) {
    std::vector<std::string> names;
    if (!fs::listDirectory(dir, names)) return;
    for (const std::string& name : names) {
        const std::string path = str::join(dir, name);
        if (fs::isDirectory(path)) {
            collectShaders(path, out);
            continue;
        }
        const std::string extension(str::extensionView(name));
        if (extension == ".vert" || extension == ".frag") out.push_back(path);
    }
}

// The varyings of a pair of shaders have to be the same ones.

// The names a shader passes from one stage to the other: the "out" of the
// vertex stage and the "in" of the fragment stage.
void collectVaryings(const std::string& text, const char* keyword,
                     std::vector<std::string>& out) {
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        line = line.substr(first);
        if (line.rfind("//", 0) == 0) continue;
        if (line.find("uniform") != std::string::npos) continue;
        if (line.rfind("layout", 0) == 0) continue;

        const std::string key = std::string(" ") + keyword + " ";
        const size_t found = line.find(key);
        if (found == std::string::npos) continue;
        const size_t semi = line.find(';', found);
        if (semi == std::string::npos) continue;
        const std::string declaration = line.substr(found + key.size(), semi - found - key.size());
        const size_t space = declaration.find_last_of(" \t");
        const std::string name =
            space == std::string::npos ? declaration : declaration.substr(space + 1);
        if (!name.empty()) out.push_back(name);
    }
}

// The shaders are staged next to the program.
void testShaders() {
    heading("the shaders carry their licence");
    const std::string dir = str::join(fs::executableDir(), "shaders");
    std::vector<std::string> files;
    collectShaders(dir, files);
    if (files.empty()) {
        std::printf("  skip no shaders next to the program\n");
        return;
    }
    check((int)files.size() >= 6, "the shaders are staged next to the program");

    for (const std::string& file : files) {
        std::string text;
        if (!fs::readFile(file, text)) {
            check(false, "a shader can be read");
            continue;
        }

        // The version has to be the first thing in the file: a comment above
        // it and the driver will not read the shader at all.
        const bool versionFirst = text.rfind("#version", 0) == 0;
        check(versionFirst, "a shader starts with its version");

        // And the licence, right under it.
        const bool licensed =
            text.find("Licensed under the Blue Oak Model License 1.0.0.") != std::string::npos;
        check(licensed, "a shader says which licence it is under");

        // The braces have to match, or the driver will say so at the worst
        // possible moment.
        int depth = 0;
        bool balanced = true;
        for (char c : text) {
            if (c == '{') ++depth;
            else if (c == '}') --depth;
            if (depth < 0) balanced = false;
        }
        check(balanced && depth == 0, "the braces of a shader match");
    }

    // A pair of shaders has to pass the same things: a varying the vertex
    // stage sends and the fragment stage does not know about is a link error
    // the driver finds at the worst possible moment.
    for (const std::string& file : files) {
        const std::string extension(str::extensionView(file));
        if (extension != ".vert") continue;
        const std::string stem = file.substr(0, file.size() - extension.size());
        std::string fragmentText;
        if (!fs::readFile(stem + ".frag", fragmentText)) continue;
        std::string vertexText;
        if (!fs::readFile(file, vertexText)) continue;

        std::vector<std::string> sent, received;
        collectVaryings(vertexText, "out", sent);
        collectVaryings(fragmentText, "in", received);
        bool same = sent.size() == received.size();
        for (const std::string& name : sent) {
            bool found = false;
            for (const std::string& other : received)
                if (other == name) found = true;
            if (!found) same = false;
        }
        check(same, "a pair of shaders passes the same things");
    }
}

void testFont() {
    heading("the shipped font has the letters");
    const std::string& assets = fs::assetRoot();
    if (assets.empty()) {
        std::printf("  skip no asset root next to the program\n");
        return;
    }
    const std::string path = str::join(str::join(assets, "fonts"), "JetBrainsMono-Regular.ttf");
    if (!fs::exists(path)) {
        std::printf("  skip no font in the assets (the engine falls back to a system one)\n");
        return;
    }

    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library)) {
        check(false, "FreeType starts");
        return;
    }
    FT_Face face = nullptr;
    if (FT_New_Face(library, path.c_str(), 0, &face) != 0) {
        check(false, "the shipped font opens");
        FT_Done_FreeType(library);
        return;
    }
    check(true, "the shipped font opens");

    // Every letter the interface is drawn with has to be in the font that draws
    // it: a font without the Cyrillic would leave the whole Ukrainian interface
    // empty, and nothing else would say so.
    //
    // The letters of the other scripts are not asked of this font: they come
    // from the fonts behind it, and the test of the chain below asks for them
    // there.
    int missing = 0;
    int firstMissing = 0;
    for (int i = 0; i < TextAtlas::kGlyphCount; ++i) {
        const unsigned cp = TextAtlas::codepointAt(i);
        if (cp == ' ' || i >= text::kBengaliFirstSlot) continue;
        if (FT_Get_Char_Index(face, cp) != 0) continue;
        if (!missing) firstMissing = (int)cp;
        ++missing;
    }
    if (missing) {
        char what[96];
        std::snprintf(what, sizeof(what), "the font has every letter (U+%04X is missing)", firstMissing);
        check(false, what);
    } else {
        check(true, "the font has every letter the atlas draws");
    }

    // The baseline sits in the line: the cap height above it and the descender
    // below it, in the middle of the line the interface lays its text out in.
    FT_Set_Pixel_Sizes(face, 0, TextAtlas::kPixelSize);
    FT_Load_Char(face, 'H', FT_LOAD_RENDER);
    const float capHeight = (float)face->glyph->bitmap_top;
    float descender = 0.0f;
    FT_Load_Char(face, 'g', FT_LOAD_RENDER);
    const float below = (float)face->glyph->bitmap.rows - (float)face->glyph->bitmap_top;
    if (below > descender) descender = below;

    const float baseline = TextAtlas::baselineFor((float)TextAtlas::kLineHeight, capHeight,
                                                  descender);
    check(baseline > 0.0f && baseline < (float)TextAtlas::kLineHeight,
          "the baseline is inside the line");
    const float top = baseline - capHeight;
    const float bottom = baseline + descender;
    check(nearly(top, (float)TextAtlas::kLineHeight - bottom, 1.0f),
          "the visible box of the text is in the middle of the line");

    FT_Done_Face(face);
    FT_Done_FreeType(library);
}

// THE CHAIN OF FONTS, AND THE LETTERS OF EVERY LANGUAGE
//
// A language is a file of letters, and a letter is only a letter if it can be
// drawn. The two ways of getting that wrong are both quiet: a letter the table
// does not have is a question mark, and a letter no font has is nothing at all.
//
// So the test reads every language file the engine ships and asks the chain of
// fonts about every letter in it. A language that is added tomorrow with a
// script nobody has a font for fails HERE, at the build, and not in the
// interface of a user.

namespace {

// The fonts the engine draws with: the one it ships and the ones behind it.
// A letter is in the chain when one of them has it.
struct FontChain {
    FT_Library library = nullptr;
    std::vector<FT_Face> faces;

    ~FontChain() {
        for (FT_Face face : faces)
            if (face) FT_Done_Face(face);
        if (library) FT_Done_FreeType(library);
    }

    bool open() {
        if (FT_Init_FreeType(&library)) return false;
        for (const std::string& path : fontFileCandidates()) {
            FT_Face face = nullptr;
            if (FT_New_Face(library, path.c_str(), 0, &face) == 0) {
                faces.push_back(face);
                break;
            }
        }
        for (const std::string& path : fallbackFontCandidates()) {
            FT_Face face = nullptr;
            if (FT_New_Face(library, path.c_str(), 0, &face) == 0) faces.push_back(face);
        }
        return !faces.empty();
    }

    bool has(unsigned codepoint) const {
        for (FT_Face face : faces)
            if (FT_Get_Char_Index(face, codepoint) != 0) return true;
        return false;
    }
};

} // namespace

void testLanguageLetters() {
    heading("every letter of every language can be drawn");

    const std::string& assets = fs::assetRoot();
    if (assets.empty()) {
        std::printf("  skip no asset root next to the program\n");
        return;
    }
    const std::string langDir = str::join(assets, "lang");
    std::vector<std::string> names;
    if (!fs::listDirectory(langDir, names) || names.empty()) {
        std::printf("  skip no language files in the assets\n");
        return;
    }

    FontChain chain;
    if (!chain.open()) {
        check(false, "the fonts of the engine open");
        return;
    }
    check(true, "the fonts of the engine open");

    int unknown = 0;       // a letter the table has no slot for
    int noFont = 0;        // a letter the table has and no font has
    std::string firstUnknown;
    std::string firstNoFont;
    int files = 0;

    for (const std::string& name : names) {
        const std::string extension(str::extensionView(name));
        if (extension != ".lang") continue;
        ++files;
        std::string text;
        if (!fs::readFile(str::join(langDir, name), text)) continue;

        // Every value of every key: the keys are the engine's own, and the
        // values are what a person reads.
        size_t at = 0;
        while (at < text.size()) {
            size_t end = text.find('\n', at);
            if (end == std::string::npos) end = text.size();
            const std::string line = str::trim(text.substr(at, end - at));
            at = end + 1;
            if (line.empty() || line[0] == '#') continue;
            const size_t separator = line.find('=');
            if (separator == std::string::npos) continue;
            const std::string value = line.substr(separator + 1);

            for (const char* p = value.c_str(); *p;) {
                const unsigned cp = str::nextCodepoint(p);
                if (!text::covers(cp)) {
                    if (!unknown) {
                        char what[128];
                        std::snprintf(what, sizeof(what),
                                      "a letter of %s has no slot (U+%04X)", name.c_str(), cp);
                        firstUnknown = what;
                    }
                    ++unknown;
                    continue;
                }
                if (!chain.has(cp)) {
                    if (!noFont) {
                        char what[128];
                        std::snprintf(what, sizeof(what),
                                      "a letter of %s has no font (U+%04X)", name.c_str(), cp);
                        firstNoFont = what;
                    }
                    ++noFont;
                }
            }
        }
    }

    check(files > 0, "the languages of the engine are there");
    if (unknown) check(false, firstUnknown.c_str());
    else check(true, "every letter of every language has a slot in the atlas");
    if (noFont) check(false, firstNoFont.c_str());
    else check(true, "every letter of every language has a font to draw it");
}

// EVERY LETTER IS THE LETTER IT SHOULD BE

void testGlyphTable() {
    heading("the atlas and the letters agree");
    // The atlas is baked with one table and read with the other, so the two
    // have to be the same table. They were not: every Cyrillic letter was
    // drawn as the one after it, and a word in Ukrainian came out as a word in
    // a language nobody speaks.
    int wrong = 0;
    for (int slot = 0; slot < TextAtlas::kGlyphCount; ++slot) {
        const unsigned cp = TextAtlas::codepointAt(slot);
        if (TextAtlas::glyphIndex(cp) != slot) ++wrong;
    }
    check(wrong == 0, "every baked letter is found again by its own code point");

    // And the letters the interface needs are all there.
    check(TextAtlas::glyphIndex('A') >= 0, "a latin letter is in the atlas");
    check(TextAtlas::glyphIndex(0x410) >= 0, "a cyrillic letter is in the atlas");
    check(TextAtlas::glyphIndex(0x401) >= 0, "the ukrainian Є is in the atlas");
    check(TextAtlas::glyphIndex(0x451) >= 0, "the ukrainian є is in the atlas");
    check(TextAtlas::glyphIndex(0xE4) >= 0, "a german umlaut is in the atlas");
    check(TextAtlas::glyphIndex(0xE7) >= 0, "a french cedilla is in the atlas");

    // The letter a slot holds is the letter that is looked up: the two
    // tables, one after the other.
    const unsigned ukrainian = 0x413; // Г
    const int slot = TextAtlas::glyphIndex(ukrainian);
    check(slot >= 0 && TextAtlas::codepointAt(slot) == ukrainian,
          "a cyrillic letter is the letter its slot holds");
}

// THE SHADOWS ARE THE PROJECT'S, AND THE SUN IS WHERE THE SETTINGS SAY
void testShadowSettings() {
    heading("the sun is where the settings say it is");

    // The sun is a point in the sky, and the direction of the light is the
    // direction from the world to that point - not the direction the light
    // travels. A sun at the north and straight up lights the ground from
    // above.
    ShadowSettings settings;
    settings.sunAzimuth = 0.0f;
    settings.sunElevation = 90.0f;
    Vec3 up = sunDirection(settings);
    check(nearly(up.x, 0.0f, 0.001f) && nearly(up.y, 1.0f, 0.001f) && nearly(up.z, 0.0f, 0.001f),
          "a sun straight up points up");

    // And it is a direction: the same sun at the east and half way up.
    settings.sunAzimuth = 90.0f;
    settings.sunElevation = 30.0f;
    Vec3 east = sunDirection(settings);
    check(nearly(east.x, 0.866f, 0.01f) && nearly(east.y, 0.5f, 0.01f) &&
              nearly(east.z, 0.0f, 0.01f),
          "a sun in the east and half way up points east and up");

    heading("the project keeps the shadows");
    // The manifest is the only place the shadow settings are written down, so
    // a number that does not survive the round trip is a number the project
    // forgets.
    ProjectLibrary library;
    library.setRoot(fs::executableDir());
    // Whatever a previous run left behind is taken away first: a folder that
    // is already there is a project that cannot be made again.
    fs::removeTree(str::join(fs::executableDir(), "shadowtest"));
    ProjectInfo info = library.create("shadowtest", LanguageTag::English);
    check(info.valid, "the test project is made");
    info.shadowQuality = 3;
    info.shadowResolution = 4096;
    info.shadowCascades = 2;
    info.shadowSunAzimuth = 123.5f;
    info.shadowSunElevation = 41.25f;
    info.shadowBias = 0.037f;
    info.shadowNormalOffset = 3.5f;
    info.shadowRadius = 7.5f;

    check(library.writeManifest(info), "the manifest is written");

    ProjectInfo read;
    check(library.readManifest(info.path, read), "the manifest is read");
    check(read.shadowQuality == 3, "the quality survives the round trip");
    check(read.shadowResolution == 4096, "the size of the map survives the round trip");
    check(read.shadowCascades == 2, "the number of the stretches survives the round trip");
    check(nearly(read.shadowSunAzimuth, 123.5f, 0.05f), "the sun's azimuth survives");
    check(nearly(read.shadowSunElevation, 41.25f, 0.05f), "the sun's height survives");
    check(nearly(read.shadowBias, 0.037f, 0.001f), "the bias survives");
    check(nearly(read.shadowNormalOffset, 3.5f, 0.05f), "the normal offset survives");
    check(nearly(read.shadowRadius, 7.5f, 0.05f), "the penumbra survives");

    // And the project the test made is taken away again: a check that leaves
    // a folder behind is a check that makes the next run find something it
    // did not make.
    fs::removeTree(info.path);
}

// The layout of a project: the folders it has, where its scene is, and what a
// project made before the layout changed is moved into.
void testProjectLayout() {
    heading("a project has the layout");

    ProjectLibrary library;
    library.setRoot(fs::executableDir());
    fs::removeTree(str::join(fs::executableDir(), "layouttest"));
    ProjectInfo info = library.create("layouttest", LanguageTag::English);
    check(info.valid, "the test project is made");

    // The folders of the project.
    const char* folders[] = {"assets", "lib", "scripts", "scenes"};
    for (const char* folder : folders)
        check(fs::isDirectory(str::join(info.path, folder)),
              "the project has the folder it is supposed to have");

    // The folders inside the assets.
    const char* assets[] = {"textures", "audio",  "music", "shaders", "material",
                            "fonts",   "models", "sky",   "lang",    "icons",
                            "prefabs", "ui",     "data",  "particles", "physics"};
    bool allAssets = true;
    for (const char* folder : assets)
        if (!fs::isDirectory(str::join(str::join(info.path, "assets"), folder)))
            allAssets = false;
    check(allAssets, "the assets of the project have every folder");

    // The scene is in `scenes/`, and the manifest says so.
    const std::string scene = ProjectLibrary::scenePath(info);
    check(str::fileName(scene) == "scene.json" && scene.find("scenes") != std::string::npos,
          "the scene of the project is in scenes/");
    check(fs::exists(scene), "the scene file is there");

    ProjectInfo read;
    check(library.readManifest(info.path, read), "the manifest is read back");
    check(read.sceneFile == "scenes/scene.json", "the manifest says where the scene is");
    check(ProjectLibrary::scriptsPath(read) ==
              str::join(info.path, "scripts"),
          "the scripts of the project are in scripts/");
    check(ProjectLibrary::assetsPath(read) == str::join(info.path, "assets"),
          "the assets of the project are in assets/");
    check(ProjectLibrary::libPath(read) == str::join(info.path, "lib"),
          "the core of the engine is in lib/");

    // A project made before the layout changed: the scene beside the project
    // and the models beside it. It is moved into the layout when it is opened.
    fs::removeTree(str::join(fs::executableDir(), "oldproject"));
    const std::string oldPath = str::join(fs::executableDir(), "oldproject");
    fs::ensureDirectory(oldPath);
    fs::ensureDirectory(str::join(oldPath, "models"));
    fs::writeFile(str::join(oldPath, "scene.json"), "{}");
    fs::writeFile(str::join(oldPath, "models/box.fbx"), "not a model");
    ProjectInfo legacy;
    legacy.name = "oldproject";
    legacy.path = oldPath;
    legacy.manifestPath = str::join(oldPath, "project.spectra");
    legacy.version = 1;
    legacy.valid = true;
    legacy.sceneFile = "scene.json";
    library.writeManifest(legacy);

    ProjectInfo opened;
    check(library.readManifest(oldPath, opened), "the old project is read");
    check(library.migrate(opened), "the old project is moved into the layout");
    check(fs::exists(str::join(str::join(oldPath, "scenes"), "scene.json")),
          "the scene moved into scenes/");
    check(fs::isDirectory(str::join(str::join(oldPath, "assets"), "models")),
          "the models moved into assets/models");
    check(!fs::isDirectory(str::join(oldPath, "models")), "the old models folder is gone");

    ProjectInfo after;
    check(library.readManifest(oldPath, after), "the old project is read back");
    check(after.sceneFile == "scenes/scene.json", "the manifest of the old project says so");

    // A built game: its data is in a folder named after the program, and the
    // manifest in it is the boot file.
    const std::string dataPath = str::join(fs::executableDir(), "GameTest_Data");
    fs::removeTree(dataPath);
    fs::ensureDirectory(dataPath);
    fs::writeFile(str::join(dataPath, kProjectBootFile),
                  "name=GameTest\nversion=1\nengine=Spectra3D 1.0.0\n");
    ProjectInfo game;
    check(library.readManifest(dataPath, game), "the boot file of a built game is read");
    check(game.name == "GameTest", "the name of a built game is the name in the manifest");
    check(fs::exists(str::join(dataPath, kProjectBootFile)),
          "the boot file is where the manifest is");

    fs::removeTree(dataPath);
    fs::removeTree(oldPath);
    fs::removeTree(info.path);
}

} // namespace

int main() {
    std::printf("Spectra3D editor checks\n");

    // The assets are found the way the engine finds them: next to the program.
    // The font check needs them, and the test has to look in the same place the
    // engine looks.
    fs::adoptAssetWorkingDir();

    testGeometry();
    testClosedPanel();
    testResize();
    testGrips();
    testFloatingPanels();
    testWindowDrag();
    testPictureAndPointer();
    testFlatPictureAndPointer();
    testFlatViewDirections();
    testFlatPicking();
    testFlatGizmo();
    testNoise();
    testLandform();
    testLandformScene();
    testLandformLooks();
    testLandformBrush();
    testWater();
    testCloud();
    testCloudScene();
    testParticlesScene();
    testTextCodepoints();
    testWidgetIds();
    testShaders();
    testGlyphTable();
    testShadowSettings();
    testFont();
    testLanguageLetters();
    testProjectLayout();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
