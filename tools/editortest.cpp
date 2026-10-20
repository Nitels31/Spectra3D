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

#include "core/Math.hpp"
#include "core/Str.hpp"
#include "platform/Filesystem.hpp"
#include "render/Camera.hpp"
#include "render/SceneRenderer.hpp"
#include "render/TextAtlas.hpp"

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
    testPictureAndPointer();
    testFont();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
