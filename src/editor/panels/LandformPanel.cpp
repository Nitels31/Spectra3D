#include "editor/Editor.hpp"

#include "core/Log.hpp"
#include "core/Math.hpp"
#include "editor/Picking.hpp"
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
//
// The ground stands where it was put, and the height of it is asked about in
// the ground's own frame: the ground is made in the middle of the world and
// then put where it belongs.
void liftBodiesAboveLandform(PhysicsWorld& world, const Landform& landform, float originX,
                             float originY, float originZ) {
    for (RigidBody& body : world.bodies) {
        // The ground itself is static and stays where it is, and so does
        // anything else that is nailed down.
        if (body.material.isStatic) continue;
        // The height of the ground is measured from its middle, and the world
        // is measured from the same place.
        const float surface = landform.heightAt(body.position.x - originX,
                                                body.position.z - originZ) +
                               originY;
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

    // --- the numbers of the shape -------------------------------------------
    // The numbers the shape that is chosen is made of. A hill has nothing to
    // say about the number of steps a plateau is cut into, and the fields of
    // the shapes that are not chosen are not shown.
    switch (landformSettings_.shape) {
    case LandformShape::Hills:
        break;

    case LandformShape::Mountains:
        ui_.propertyFloat(ui, kLandformLowland, x, y, width, text("landform.lowland"),
                          landformSettings_.lowland, 0.01f, 0.0f, 1.0f);
        y += kFieldHeight;
        break;

    case LandformShape::Island:
        ui_.propertyFloat(ui, kLandformShore, x, y, width, text("landform.shore"),
                          landformSettings_.shore, 0.01f, 0.0f, 0.95f);
        y += kFieldHeight;
        break;

    case LandformShape::Plateau:
        ui_.propertyInt(ui, kLandformLevels, x, y, width, text("landform.levels"),
                        landformSettings_.levels, 1, 16, 0.25f);
        y += kFieldHeight;
        ui_.propertyFloat(ui, kLandformRoundness, x, y, width, text("landform.roundness"),
                          landformSettings_.roundness, 0.01f, 0.02f, 1.0f);
        y += kFieldHeight;
        // How much of the ordinary ground is put on the plateau. None of it
        // leaves the tops flat as they were.
        ui_.propertyFloat(ui, kLandformGround, x, y, width, text("landform.ground"),
                          landformSettings_.ground, 0.01f, 0.0f, 1.0f);
        y += kFieldHeight;
        break;

    case LandformShape::Canyon:
        ui_.propertyFloat(ui, kLandformCut, x, y, width, text("landform.cut"),
                          landformSettings_.cut, 0.01f, 0.0f, 1.0f);
        y += kFieldHeight;
        ui_.propertyFloat(ui, kLandformRiver, x, y, width, text("landform.river"),
                          landformSettings_.river, 0.002f, 0.01f, 0.50f);
        y += kFieldHeight;
        ui_.propertyFloat(ui, kLandformMeander, x, y, width, text("landform.meander"),
                          landformSettings_.meander, 0.02f, 0.0f, 2.0f);
        y += kFieldHeight;
        break;

    case LandformShape::Dunes:
        ui_.propertyFloat(ui, kLandformStretch, x, y, width, text("landform.stretch"),
                          landformSettings_.stretch, 0.05f, 0.20f, 4.0f);
        y += kFieldHeight;
        break;
    }

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

    // The feature size and the variation are small numbers, and a small
    // number is a number that has to be shown with more of itself: a
    // variation of a thousandth and one of a hundredth both say "0.01" when
    // they are shown with two.
    ui_.propertyFine(ui, kLandformFrequency, x, y, width, text("landform.frequency"),
                      landformSettings_.frequency, 0.0002f, 0.001f, 1.0f);
    y += kFieldHeight;
    // How much the ground may differ from the one before it. The number in
    // the field is the variation of the ground that is NEXT, and a new one is
    // drawn every time a ground is made: the ground is not the same ground
    // twice, and a number typed here is the number that is used.
    ui_.propertyFine(ui, kLandformVariation, x, y, width, text("landform.variation"),
                      landformSettings_.variation, 0.0002f, 0.0f, 0.05f);
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
    // The little bumps on the surface of the ground, and how much the valleys
    // are darkened. Together they are the difference between a shape and a
    // piece of land.
    ui_.propertyFloat(ui, kLandformDetail, x, y, width, text("landform.detail"),
                      landformSettings_.detail, 0.005f, 0.0f, 1.0f);
    y += kFieldHeight;
    ui_.propertyFloat(ui, kLandformShading, x, y, width, text("landform.shading"),
                      landformSettings_.shading, 0.005f, 0.0f, 1.0f);
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

    // --- where the ground lands --------------------------------------------
    // Where the new ground is put. The ground is a body in the scene and it
    // can be dragged where it belongs afterwards, but the ground that is made
    // here lands where it is told to, and the middle of the world is not
    // always where the game is.
    ui.textFit(x, y, width, text("landform.place"), theme::textDim);
    y += theme::textLineHeight + 4.0f;
    {
        const float half = (width - 8.0f) * 0.5f;
        ui_.propertyFloat(ui, kLandformPlaceX, x, y, half, text("landform.placeX"),
                          landformPlaceX_, 0.5f, -1000.0f, 1000.0f);
        ui_.propertyFloat(ui, kLandformPlaceZ, x + half + 8.0f, y, half, text("landform.placeZ"),
                          landformPlaceZ_, 0.5f, -1000.0f, 1000.0f);
        y += kFieldHeight;
    }

    // --- the colour --------------------------------------------------------
    // Whether the ground is coloured by its biome, or all of it is one colour
    // with the picture on it.
    ui_.checkbox(ui, kLandformBiomes, x, y, width, text("landform.biomes"),
                 landformSettings_.biomes);
    y += 22.0f;

    if (landformSettings_.biomes) {
        // --- the biome ------------------------------------------------------
        // Which colours the ground comes out in. A biome is a set of colours
        // and nothing else: the ground is made the same way in every one of
        // them.
        //
        // The swatches are the colours of the biomes themselves, so the row
        // is a picture of the choice rather than a list of names to be read.
        y += 2.0f;
        ui.textFit(x, y, width, text("landform.biome"), theme::textDim);
        y += theme::textLineHeight + 4.0f;
        {
            const float swatch =
                std::min(28.0f, (width - (float)(kLandformBiomeCount - 1) * 2.0f) /
                                     (float)kLandformBiomeCount);
            for (int i = 0; i < kLandformBiomeCount; ++i) {
                const float sx = x + (float)i * (swatch + 2.0f);
                const Rect hit{sx, y, swatch, swatch};
                if (ui_.button(ui, kLandformBiome + i, hit.x, hit.y, hit.width, hit.height, ""))
                    landformSettings_.biome = (LandformBiome)i;

                // The swatch is the biome itself in three bands: the bottom of
                // the ground, the ground, and the top of it. A row of eight of
                // them is a picture of the choice, and a picture needs no
                // reading.
                const LandformPalette& palette = landformPalette((LandformBiome)i);
                const float band = hit.height / 3.0f;
                const float* const bands[3] = {palette.low, palette.mid, palette.high};
                for (int b = 0; b < 3; ++b)
                    ui.fillRect(hit.x, hit.y + hit.height - (float)(b + 1) * band, hit.width,
                                band, Color{bands[b][0], bands[b][1], bands[b][2]});

                // The one that is chosen is the one with a frame around it.
                if ((int)landformSettings_.biome == i) {
                    ui.fillRect(hit.x - 2.0f, hit.y - 2.0f, hit.width + 4.0f, 1.0f, theme::accent);
                    ui.fillRect(hit.x - 2.0f, hit.y + hit.height + 1.0f, hit.width + 4.0f, 1.0f,
                                theme::accent);
                }
            }
            y += swatch + 8.0f;
            // The name of the biome that is chosen, under the swatches: the
            // colours are a picture, and the picture needs a word under it.
            ui.textFit(x, y, width, text(landformBiomeName(landformSettings_.biome)), theme::text);
            y += theme::textLineHeight + 4.0f;
        }
    } else {
        y += 2.0f;
        ui.textFit(x, y, width, text("landform.color"), theme::textDim);
        y += theme::textLineHeight + 4.0f;
        const float swatch = std::min(28.0f, (width - 10.0f) / 6.0f);
        for (int i = 0; i < kLandformColors; ++i) {
            const float sx = x + (float)i * (swatch + 2.0f);
            const Rect hit{sx, y, swatch, swatch};
            if (ui_.button(ui, kLandformColor + i, hit.x, hit.y, hit.width, hit.height, "")) {
                // All three of the numbers: a colour that is only red is not
                // the colour of the swatch that was pressed.
                landformSettings_.color[0] = kLandformColorValues[i][0];
                landformSettings_.color[1] = kLandformColorValues[i][1];
                landformSettings_.color[2] = kLandformColorValues[i][2];
            }
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

    // --- the brush ----------------------------------------------------------
    // What a stroke of the hand does to the ground. The brush is armed with
    // the button at the end of it, and then the hand in the viewport paints
    // the ground instead of picking it.
    y += 2.0f;
    ui.textFit(x, y, width, text("landform.brush"), theme::textDim);
    y += theme::textLineHeight + 4.0f;
    {
        // Four brushes in a row: the names are words, and a word does not fit
        // in a quarter of a panel.
        const float brushWidth = (width - 12.0f) / 4.0f;
        for (int i = 0; i < 4; ++i) {
            const Rect hit{x + (float)i * (brushWidth + 4.0f), y, brushWidth, kShapeRowHeight};
            const bool on = (int)landformBrush_ == i;
            if (ui_.button(ui, kLandformBrush + i, hit.x, hit.y, hit.width, hit.height,
                           text(landformBrushName((LandformBrush)i)), on))
                landformBrush_ = (LandformBrush)i;
        }
        y += kShapeRowHeight + 4.0f;
    }
    // How wide the stroke is, and how much it does.
    ui_.propertyFloat(ui, kLandformBrushRadius, x, y, width, text("landform.brushRadius"),
                      landformBrushRadius_, 0.1f, 0.5f, 60.0f);
    y += kFieldHeight;
    ui_.propertyFloat(ui, kLandformBrushStrength, x, y, width, text("landform.brushStrength"),
                      landformBrushStrength_, 0.05f, 0.1f, 40.0f);
    y += kFieldHeight;
    {
        // The button that puts the brush in the hand.
        const Rect hit{x, y, width, kButtonHeight};
        if (ui_.button(ui, kLandformPaint, hit.x, hit.y, hit.width, hit.height,
                       text(landformPainting_ ? "landform.paintStop" : "landform.paint"),
                       landformPainting_))
            landformPainting_ = !landformPainting_;
        y += kButtonHeight + 4.0f;
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
        // And so does the variation: the ground that was just made used the
        // number that was in the field, and the next one gets a new number
        // drawn for it. Two grounds made one after the other are not the same
        // ground twice.
        landformSettings_.variation = landformVariation();
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
    // The settings the ground was made of travel with the mesh, so a scene
    // that is saved says how the ground was made and the ground is made again
    // when the scene is opened. Without them the ground would come back as a
    // box the size of the ground.
    asset.landform = landform_.settings();

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
    // The ground the panel is working on: the brush paints this one until the
    // hand is over another ground.
    landformMesh_ = mesh;

    // The body it becomes: a mesh body, with the size the mesh says it has,
    // where the panel says the ground lands.
    //
    // The body stands at the middle of the ground: the mesh is drawn around
    // its own middle, and the ground is the height it was asked to be.
    const MeshAsset* made = meshes_.at(mesh);
    RigidBody body;
    body.shape = Shape::Mesh;
    body.mesh = mesh;
    body.halfExtents = made ? made->half : Vec3{1.0f, 1.0f, 1.0f};
    body.position = Vec3{landformPlaceX_, (settings.minHeight + settings.maxHeight) * 0.5f,
                         landformPlaceZ_};
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
    liftBodiesAboveLandform(world, landform_, landformPlaceX_,
                            (settings.minHeight + settings.maxHeight) * 0.5f, landformPlaceZ_);

    // And it is selected, so the inspector shows what was just made.
    activeBody_ = (int)world.bodies.size() - 1;
    selectedBodies_.assign(1, activeBody_);
    markSceneDirty();
    Log::info("Landform: %dx%d ground made, %d squares, %d thread(s)", (int)settings.width,
              (int)settings.depth, settings.cells * settings.cells, settings.threads);
}

// Paints the ground under the pointer.
//
// The brush follows the hand, and the hand is over the ground. The ground
// under the ray is found with the box of the ground first - it is cheap, and
// it says where a ground could be - and then with the heights of it, because
// a box is not a hill and the brush paints hills.
void Editor::paintGround(PhysicsWorld& world, const Vec3& origin, const Vec3& direction,
                         float dt) {
    // The ground the ray is over: the nearest one whose box the ray meets.
    int found = -1;
    float nearest = 0.0f;
    for (int i = 0; i < (int)world.bodies.size(); ++i) {
        const RigidBody& body = world.bodies[i];
        if (!body.hasMesh()) continue;
        const MeshAsset* asset = meshes_.at(body.mesh);
        if (!asset || !asset->landform) continue;
        float distance = 0.0f;
        if (!picking::intersectObb(origin, direction, body, distance)) continue;
        if (found < 0 || distance < nearest) {
            nearest = distance;
            found = i;
        }
    }
    if (found < 0) return;

    RigidBody& body = world.bodies[(size_t)found];
    const MeshAsset* asset = meshes_.at(body.mesh);
    if (!asset) return;

    // The ground the panel is working on: the one that was made last, or the
    // one under the hand. The mesh it belongs to is remembered, so that the
    // ground is not read again for every frame of a stroke.
    if (landformMesh_ != body.mesh) {
        landform_.build(*asset->landform);
        if (!asset->landformHeights.empty()) landform_.setHeights(asset->landformHeights);
        landformMesh_ = body.mesh;
    }

    // The ray in the ground's own frame: the ground is made in the middle of
    // the world and then put where it belongs.
    const Quat inverse = body.orientation.conjugate();
    const Vec3 localOrigin = rotate(inverse, origin - body.position);
    const Vec3 localDirection = rotate(inverse, direction);

    float distance = 0.0f;
    if (!landform_.rayHit(localOrigin, localDirection, distance)) return;

    // Where the ray met the ground, in the ground's own frame.
    const Vec3 point = localOrigin + localDirection * distance;
    const float x = point.x;
    const float z = point.z;

    // The stroke begins where the hand was put down, and the flatten brush
    // levels the ground to the height it had there.
    if (ui_.pointerPressed()) {
        landform_.beginStroke(x, z);
        // The ground as it was before the stroke: it is what the stroke is
        // taken back to.
        paintBefore_ = landform_.heights();
        paintMesh_ = body.mesh;
    }

    // How much the stroke does. The strength is in metres a second, so the
    // brush does the same amount of work on a slow machine and on a fast one.
    const float amount = landformBrushStrength_ * dt;
    if (!landform_.paint(x, z, landformBrushRadius_, amount, landformBrush_)) return;

    // The mesh of the ground is the ground now. It is put back where it was -
    // the body that stands on it keeps pointing at it - and the picture on it
    // is the picture it had.
    MeshAsset mesh = landform_.toMesh();
    mesh.name = meshes_.nameOf(body.mesh);
    if (const MeshAsset* before = meshes_.at(body.mesh))
        for (size_t part = 0; part < mesh.parts.size() && part < before->parts.size(); ++part)
            mesh.parts[part].texture = before->parts[part].texture;
    mesh.landform = *asset->landform;
    mesh.landformHeights = landform_.heights();
    meshes_.update(body.mesh, std::move(mesh));

    // And the body: the ground is a little taller now, and the box of it with
    // it.
    if (const MeshAsset* made = meshes_.at(body.mesh)) body.halfExtents = made->half;
    body.syncPrevTransform();
    markSceneDirty();
}

// Puts the heights of a ground back into its mesh.
//
// The ground is a heightfield inside a mesh, and the mesh is in the registry
// of meshes and not in the world: this is what takes a stroke of the brush
// back, and what makes it again.
void Editor::restoreGroundHeights(PhysicsWorld& world, int meshIndex,
                                  const std::vector<float>& heights) {
    if (meshIndex < 0 || heights.empty()) return;
    const MeshAsset* asset = meshes_.at(meshIndex);
    if (!asset || !asset->landform) return;

    // The ground is made again with the heights that are given to it, and the
    // mesh is put back where it was.
    Landform ground;
    ground.build(*asset->landform);
    ground.setHeights(heights);

    MeshAsset mesh = ground.toMesh();
    mesh.name = meshes_.nameOf(meshIndex);
    for (size_t part = 0; part < mesh.parts.size() && part < asset->parts.size(); ++part)
        mesh.parts[part].texture = asset->parts[part].texture;
    mesh.landform = *asset->landform;
    mesh.landformHeights = ground.heights();
    meshes_.update(meshIndex, std::move(mesh));

    // And the body that stands on the ground: the ground is a little taller
    // or shorter now, and the box of it with it.
    for (RigidBody& body : world.bodies) {
        if (body.mesh != meshIndex) continue;
        if (const MeshAsset* made = meshes_.at(meshIndex)) body.halfExtents = made->half;
        body.syncPrevTransform();
    }
    markSceneDirty();
}

// The stroke is over: what it did is one entry on the undo stack.
void Editor::endPaintStroke() {
    if (paintMesh_ < 0 || paintBefore_.empty()) {
        paintMesh_ = -1;
        paintBefore_.clear();
        return;
    }
    // The ground as it is now, and the ground as it was: the two are swapped
    // as the stroke is taken back and made again.
    UndoStep step;
    step.kind = UndoStep::Kind::Painted;
    step.index = paintMesh_;
    step.heightsBefore = paintBefore_;
    step.heightsAfter = landform_.heights();
    pending_.steps.push_back(std::move(step));
    commitUndo();

    paintMesh_ = -1;
    paintBefore_.clear();
}

} // namespace s3d
