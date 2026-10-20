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
#include "platform/Filesystem.hpp"
#include "platform/Window.hpp"
#include "render/Camera.hpp"
#include "render/SceneRenderer.hpp"
#include "render/TextAtlas.hpp"
#include "scene/Landform.hpp"
#include "scene/Noise.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cmath>
#include <cstdio>
#include <string>

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
        const float y = part.vertices[i + kMeshPositionOffset + 1];
        lowest = std::fmin(lowest, y);
        highest = std::fmax(highest, y);
    }
    check(lowest >= settings.minHeight - 0.01f && highest <= settings.maxHeight + 0.01f,
          "the ground stays between the heights that were asked for");
    check(highest - lowest > (settings.maxHeight - settings.minHeight) * 0.9f,
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
}

// THE CHARACTERS A FIELD CAN HOLD ARE THE CHARACTERS THE ENGINE CAN DRAW

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
        {Editor::kInspectorDock, Editor::kInspectorDock, "the inspector"},
        {Editor::kInspectorProperty, Editor::kInspectorProperty + 39,
         "the inspector's numbers"},
        {Editor::kFilterField, Editor::kConsoleFilter, "the filters"},
        {Editor::kSceneHeader, Editor::kSceneHeader, "the scene header"},
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

    // Every letter the atlas draws has to be in the font: a font without the
    // Cyrillic would leave the whole Ukrainian interface empty, and nothing
    // else would say so.
    int missing = 0;
    int firstMissing = 0;
    for (int i = 0; i < TextAtlas::kGlyphCount; ++i) {
        const unsigned cp = TextAtlas::codepointAt(i);
        if (cp == ' ' || FT_Get_Char_Index(face, cp) != 0) continue;
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
    testPictureAndPointer();
    testNoise();
    testLandform();
    testTextCodepoints();
    testWidgetIds();
    testShaders();
    testShadowSettings();
    testFont();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
