#include "editor/Editor.hpp"

#include "core/Log.hpp"
#include "core/Math.hpp"
#include "scene/Landform.hpp"
#include "ui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace s3d {
namespace {

constexpr float kHeaderHeight = 24.0f;
constexpr float kFieldHeight = 24.0f;
constexpr float kButtonHeight = 26.0f;
// The height of one row of the shape buttons.
constexpr float kShapeRowHeight = 24.0f;

// The colours the ground can be. The picture is multiplied by the one that is
// chosen, so these are tints of the grass rather than colours of their own.
constexpr int kLandformColors = 6;
constexpr float kLandformColorValues[kLandformColors][3] = {
    {0.55f, 0.72f, 0.42f},  // grass
    {0.62f, 0.50f, 0.34f},  // earth
    {0.72f, 0.72f, 0.74f},  // stone
    {0.86f, 0.88f, 0.92f},  // snow
    {0.48f, 0.42f, 0.30f},  // mud
    {0.85f, 0.85f, 0.85f},  // as the picture is
};

// The next seed.
//
// The same seed is the same ground, so a ground that is made twice with the
// same seed is the same ground twice. The seed moves on after every ground,
// and the next one is a different one - which is what a button that says
// "make a ground" is expected to do.
int nextSeed(int seed) {
    const unsigned value = (unsigned)seed * 1664525u + 1013904223u;
    return (int)(value % 999983u) + 1;
}

// Puts the bodies that are inside the new ground on top of it.
//
// A body that is inside the ground is a body the physics pushes out of it, and
// it comes out upwards - which looks exactly like a gravity that points the
// wrong way, and is nothing of the kind.
void liftBodiesAboveLandform(PhysicsWorld& world, const Landform& landform) {
    for (RigidBody& body : world.bodies) {
        // The ground itself is static and stays where it is, and so does
        // anything else that is nailed down.
        if (body.material.isStatic) continue;
        const float surface = landform.heightAt(body.position.x, body.position.z);
        const float bottom = body.position.y - body.halfExtents.y;
        if (bottom >= surface) continue;
        body.position.y += surface - bottom;
        body.syncPrevTransform();
    }
}

} // namespace

// The Landform panel: the shape of the ground, made of noise.
//
// It is a panel like the others - dragged by its title bar, resized by its
// edges, closed with the X, back from the View menu - because a panel that
// cannot be moved is a panel that is in the way of the work.
//
// The landform it makes lands in the scene as an ordinary body: a mesh like any
// other, so the renderer and the physics do not know or care that this one was
// not read from a file.
void Editor::drawLandformPanel(UiRenderer& ui, PhysicsWorld& world, const Rect& dock) {
    if (dock.width <= 0.0f || dock.height <= 0.0f) return;

    ui.fillRect(dock.x, dock.y, dock.width, kHeaderHeight, theme::panelDark);
    ui.text(dock.x + 18.0f, dock.y + 5.0f, text("tab.landform"), theme::text);
    ui.fillRect(dock.x, dock.y + kHeaderHeight - 1.0f, dock.width, 1.0f, theme::separator);

    // The panel is a body and a bar. The body scrolls - a floating window can
    // be made small, and the fields that do not fit have to be reachable
    // rather than cut off - and the bar at the bottom never moves: the button
    // that makes the ground has to be there whatever the size of the window,
    // because a button under the edge of its own panel is a button that
    // cannot be pressed.
    const float barHeight = kButtonHeight + 6.0f + theme::textLineHeight + 4.0f;
    const Rect body{dock.x, dock.y + kHeaderHeight, dock.width,
                    std::fmax(0.0f, dock.height - kHeaderHeight - barHeight)};

    // The content is taller than the panel often enough to be worth a wheel:
    // the height of it is measured while it is drawn, and the offset is
    // clamped against the height of the last frame - the same trick the
    // inspector uses, and for the same reason: the total is only known once
    // the sections have been laid out.
    const float maxScroll = std::fmax(0.0f, landformContentHeight_ - body.height);
    landformScroll_ = clampf(landformScroll_, 0.0f, maxScroll);
    if (ui_.hovered(body) && ui_.scrollDelta() != 0.0)
        landformScroll_ = clampf(landformScroll_ - (float)ui_.scrollDelta() * 24.0f, 0.0f,
                                 maxScroll);

    ui.pushClip(body);
    ui_.pushClip(body);

    float y = dock.y + kHeaderHeight + 8.0f - landformScroll_;
    const float width = dock.width - 16.0f;
    const float x = dock.x + 8.0f;

    // --- the shape ---------------------------------------------------------
    ui.textFit(x, y, width, text("landform.shape"), theme::textDim);
    y += theme::textLineHeight + 4.0f;

    // Three shapes in a row, two rows: the names are words and words do not
    // fit in a sixth of a panel.
    constexpr int kShapesPerRow = 3;
    const float shapeWidth = (width - 8.0f) / (float)kShapesPerRow;
    for (int i = 0; i < kLandformShapeCount; ++i) {
        const int col = i % kShapesPerRow;
        const int row = i / kShapesPerRow;
        const Rect hit{x + (float)col * (shapeWidth + 4.0f),
                       y + (float)row * (kShapeRowHeight + 4.0f), shapeWidth, kShapeRowHeight};
        const bool on = (int)landformSettings_.shape == i;
        if (ui_.button(ui, kLandformShape + i, hit.x, hit.y, hit.width, hit.height,
                       text(landformShapeName((LandformShape)i)), on))
            landformSettings_.shape = (LandformShape)i;
    }
    y += (float)((kLandformShapeCount + kShapesPerRow - 1) / kShapesPerRow) *
             (kShapeRowHeight + 4.0f) +
         2.0f;

    // --- the noise ---------------------------------------------------------
    // The seed and the button that gives a new one. The same seed is the same
    // ground everywhere, so the number is worth showing and worth keeping.
    ui_.propertyInt(ui, kLandformSeed, x, y, width - 40.0f, text("landform.seed"),
                    landformSeed_, 0, 999999, 1.0f);
    {
        const Rect dice{x + width - 36.0f, y, 36.0f, theme::fieldHeight};
        if (ui_.button(ui, kLandformRandom, dice.x, dice.y, dice.width, dice.height, "?"))
            landformSeed_ = nextSeed(landformSeed_);
    }
    y += kFieldHeight;

    ui_.propertyFloat(ui, kLandformFrequency, x, y, width, text("landform.frequency"),
                      landformSettings_.frequency, 0.002f, 0.001f, 1.0f);
    y += kFieldHeight;
    ui_.propertyInt(ui, kLandformOctaves, x, y, width, text("landform.octaves"),
                    landformSettings_.octaves, 1, 12, 0.25f);
    y += kFieldHeight;
    ui_.propertyFloat(ui, kLandformWarp, x, y, width, text("landform.warp"),
                      landformSettings_.warp, 0.01f, 0.0f, 4.0f);
    y += kFieldHeight;
    ui_.propertyFloat(ui, kLandformRidge, x, y, width, text("landform.ridge"),
                      landformSettings_.ridge, 0.01f, 0.0f, 1.0f);
    y += kFieldHeight;
    ui_.propertyInt(ui, kLandformErosion, x, y, width, text("landform.erosion"),
                    landformSettings_.erosion, 0, 24, 0.25f);
    y += kFieldHeight;
    ui_.propertyFloat(ui, kLandformSmoothing, x, y, width, text("landform.smoothing"),
                      landformSettings_.smoothing, 0.02f, 0.0f, 1.0f);
    y += kFieldHeight;

    // --- the size ----------------------------------------------------------
    ui_.propertyFloat(ui, kLandformWidth, x, y, width, text("landform.width"),
                      landformSettings_.width, 0.5f, 1.0f, 1000.0f);
    y += kFieldHeight;
    ui_.propertyFloat(ui, kLandformDepth, x, y, width, text("landform.depth"),
                      landformSettings_.depth, 0.5f, 1.0f, 1000.0f);
    y += kFieldHeight;
    ui_.propertyFloat(ui, kLandformMinHeight, x, y, width, text("landform.minHeight"),
                      landformSettings_.minHeight, 0.05f, -1000.0f, 1000.0f);
    y += kFieldHeight;
    ui_.propertyFloat(ui, kLandformMaxHeight, x, y, width, text("landform.maxHeight"),
                      landformSettings_.maxHeight, 0.05f, -1000.0f, 1000.0f);
    y += kFieldHeight;
    ui_.propertyFloat(ui, kLandformCells, x, y, width, text("landform.cells"),
                      landformCells_, 1.0f, 2.0f, 256.0f);
    y += kFieldHeight;

    // --- the colour --------------------------------------------------------
    // Whether the ground is coloured by its height and its steepness, or all
    // of it is one colour with the picture on it.
    ui_.checkbox(ui, kLandformBiomes, x, y, width, text("landform.biomes"),
                 landformSettings_.biomes);
    y += 22.0f;

    if (!landformSettings_.biomes) {
        y += 2.0f;
        ui.textFit(x, y, width, text("landform.color"), theme::textDim);
        y += theme::textLineHeight + 4.0f;
        const float swatch = std::min(28.0f, (width - 10.0f) / 6.0f);
        for (int i = 0; i < kLandformColors; ++i) {
            const float sx = x + (float)i * (swatch + 2.0f);
            const Rect hit{sx, y, swatch, swatch};
            if (ui_.button(ui, kLandformColor + i, hit.x, hit.y, hit.width, hit.height, ""))
                landformSettings_.color[0] = kLandformColorValues[i][0];
            ui.fillRect(hit.x, hit.y, hit.width, hit.height,
                        Color{kLandformColorValues[i][0], kLandformColorValues[i][1],
                              kLandformColorValues[i][2]});
            // The one that is chosen is the one with a frame around it.
            if (landformSettings_.color[0] == kLandformColorValues[i][0] &&
                landformSettings_.color[1] == kLandformColorValues[i][1] &&
                landformSettings_.color[2] == kLandformColorValues[i][2]) {
                ui.fillRect(hit.x - 2.0f, hit.y - 2.0f, hit.width + 4.0f, 1.0f, theme::accent);
                ui.fillRect(hit.x - 2.0f, hit.y + hit.height + 1.0f, hit.width + 4.0f, 1.0f,
                            theme::accent);
            }
        }
        y += swatch + 8.0f;
    }

    // How tall the panel wanted to be, measured on the way out.
    landformContentHeight_ = y + landformScroll_ - body.y + 8.0f;

    ui_.popClip();
    ui.popClip();

    // --- the bar ------------------------------------------------------------
    // The button and the hint, at the bottom of the panel, out of the way of
    // the scroll: the button that makes the ground is always in sight and
    // always where it can be pressed.
    const float barY = dock.y + dock.height - barHeight;
    ui.fillRect(dock.x, barY, dock.width, 1.0f, theme::separator);
    const Rect button{dock.x + 8.0f, barY + 6.0f, dock.width - 16.0f, kButtonHeight};
    const bool generate = ui_.button(ui, kLandformGenerate, button.x, button.y, button.width,
                                     button.height, text("landform.generate"));
    if (generate) {
        makeLandform(world);
        // And the seed moves on, so that the next ground is a different one.
        // The number in the field is the seed of the ground that is NEXT: the
        // seed of the ground that was just made is the number that was in the
        // field a moment ago, and typing it back makes the same ground again.
        landformSeed_ = nextSeed(landformSeed_);
    }
    ui.textFit(dock.x + 8.0f, button.y + kButtonHeight + 4.0f, dock.width - 16.0f,
               text("landform.hint"), theme::textDim);
}

void Editor::makeLandform(PhysicsWorld& world) {
    // The landform's own copy of the settings, with the numbers the fields
    // kept, and the threads the user chose in the settings.
    LandformSettings settings = landformSettings_;
    settings.cells = (int)landformCells_;
    settings.seed = (unsigned)landformSeed_;
    settings.threads = loaderThreads_;

    // The ground itself: the heightfield first, so that anything put on it
    // afterwards can be put ON it.
    // Whatever was being changed before is finished first: one change is one
    // entry on the undo stack.
    commitUndo();
    landform_.build(settings);

    MeshAsset asset = landform_.toMesh();
    // The name of the mesh is the seed, and it has to be: the registry hands
    // back the mesh it already has under a name, so a ground that is called
    // "landform" twice is the same ground twice - the second one would be the
    // first one with a different name on it, and the button would look like
    // it did nothing.
    asset.name = "landform " + std::to_string(settings.seed);

    // The picture on the ground. The texture tiles, so the grass is grass and
    // not one photograph stretched over a hundred metres. The biomes carry
    // their own colours, and a picture on top of them only muddies them.
    if (!settings.biomes && !settings.texture.empty()) {
        const int texture = meshes_.loadTexture(settings.texture, /*repeat=*/true);
        if (texture >= 0 && !asset.parts.empty()) asset.parts[0].texture = texture;
    }

    const int mesh = meshes_.importMesh(std::move(asset));
    if (mesh < 0) {
        Log::error("Landform: the ground could not be made");
        return;
    }

    // The body it becomes: a mesh body, at the origin, with the size the mesh
    // says it has.
    const MeshAsset* made = meshes_.at(mesh);
    RigidBody body;
    body.shape = Shape::Mesh;
    body.mesh = mesh;
    body.halfExtents = made ? made->half : Vec3{1.0f, 1.0f, 1.0f};
    body.position = Vec3{0.0f, settings.minHeight, 0.0f};
    // The ground does not fall. A piece of ground that falls is a box, and a box
    // that falls through the floor takes the scene with it.
    body.material.isStatic = true;
    body.configure(body.mass);
    body.syncPrevTransform();
    world.bodies.push_back(body);
    world.wakeAll();
    // Remembered, so that Ctrl+Z can take the ground away again.
    rememberAddition(world, (int)world.bodies.size() - 1);
    commitUndo();

    // And whatever was standing where the ground now is is put on top of it.
    liftBodiesAboveLandform(world, landform_);

    // And it is selected, so the inspector shows what was just made.
    activeBody_ = (int)world.bodies.size() - 1;
    selectedBodies_.assign(1, activeBody_);
    markSceneDirty();
    Log::info("Landform: %dx%d ground made, %d squares, %d thread(s)", (int)settings.width,
              (int)settings.depth, settings.cells * settings.cells, settings.threads);
}

} // namespace s3d
