#include "editor/Editor.hpp"

#include "core/Log.hpp"
#include "core/Math.hpp"
#include "scene/Cloud.hpp"
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

// THE COLOURS A CLOUD CAN BE.
//
// Each one is a pair: the colour the sun reaches and the colour of the parts
// of the cloud the sun does not reach. The bottom of a cloud is grey and the
// top is white, and the pair is where that comes from.
struct CloudPalette {
    float lit[3];
    float shade[3];
};

constexpr int kCloudPalettes = 4;
constexpr CloudPalette kCloudPaletteValues[kCloudPalettes] = {
    {{1.00f, 1.00f, 1.00f}, {0.62f, 0.68f, 0.78f}},  // white
    {{1.00f, 0.92f, 0.80f}, {0.74f, 0.55f, 0.60f}},  // sunset
    {{0.84f, 0.86f, 0.90f}, {0.38f, 0.40f, 0.46f}},  // storm
    {{0.74f, 0.82f, 0.90f}, {0.44f, 0.50f, 0.60f}},  // rain
};

// The next seed.
//
// The same seed is the same cloud, so a cloud that is made twice with the
// same seed is the same cloud twice. The seed moves on after every cloud,
// and the next one is a different one - which is what a button that says
// "make a cloud" is expected to do.
int nextSeed(int seed) {
    const unsigned value = (unsigned)seed * 1664525u + 1013904223u;
    return (int)(value % 999983u) + 1;
}

// The palette a cloud is drawn in, as the index of it. The one that is chosen
// is the one whose lit colour is the one of the cloud.
int paletteOf(const CloudSettings& settings) {
    for (int i = 0; i < kCloudPalettes; ++i)
        if (settings.color[0] == kCloudPaletteValues[i].lit[0] &&
            settings.color[1] == kCloudPaletteValues[i].lit[1] &&
            settings.color[2] == kCloudPaletteValues[i].lit[2])
            return i;
    return 0;
}

} // namespace

// The Cloud panel: the sky, made of noise.
//
// It is a panel like the others - dragged by its title bar, resized by its
// edges, closed with the X, back from the View menu - because a panel that
// cannot be moved is a panel that is in the way of the work.
//
// The cloud it makes lands in the scene as an ordinary body: a mesh like any
// other, so the renderer and the physics do not know or care that this one was
// not read from a file.
void Editor::drawCloudPanel(UiRenderer& ui, PhysicsWorld& world, const Rect& dock) {
    if (dock.width <= 0.0f || dock.height <= 0.0f) return;

    ui.fillRect(dock.x, dock.y, dock.width, kHeaderHeight, theme::panelDark);
    ui.text(dock.x + 18.0f, dock.y + 5.0f, text("tab.cloud"), theme::text);
    ui.fillRect(dock.x, dock.y + kHeaderHeight - 1.0f, dock.width, 1.0f, theme::separator);

    // The panel is a body and a bar. The body scrolls and the bar at the
    // bottom never moves: the button that makes the cloud has to be there
    // whatever the size of the window.
    const float barHeight = kButtonHeight + 6.0f + theme::textLineHeight + 4.0f;
    const Rect body{dock.x, dock.y + kHeaderHeight, dock.width,
                    std::fmax(0.0f, dock.height - kHeaderHeight - barHeight)};

    const float maxScroll = std::fmax(0.0f, cloudContentHeight_ - body.height);
    cloudScroll_ = clampf(cloudScroll_, 0.0f, maxScroll);
    if (ui_.hovered(body) && ui_.scrollDelta() != 0.0)
        cloudScroll_ = clampf(cloudScroll_ - (float)ui_.scrollDelta() * 24.0f, 0.0f, maxScroll);

    ui.pushClip(body);
    ui_.pushClip(body);

    float y = dock.y + kHeaderHeight + 8.0f - cloudScroll_;
    const float width = dock.width - 16.0f;
    const float x = dock.x + 8.0f;

    // --- the way it is drawn ------------------------------------------------
    // Two buttons: a flat cloud, which is a picture on a plane, and a cloud
    // of puffs, which is a volume. They are not two settings of one thing.
    ui.textFit(x, y, width, text("cloud.mode"), theme::textDim);
    y += theme::textLineHeight + 4.0f;

    {
        const float half = (width - 4.0f) * 0.5f;
        const Rect flat{x, y, half, kShapeRowHeight};
        const Rect puffy{x + half + 4.0f, y, half, kShapeRowHeight};
        if (ui_.button(ui, kCloudMode, flat.x, flat.y, flat.width, flat.height,
                       text(cloudModeName(CloudMode::Flat)),
                       cloudSettings_.mode == CloudMode::Flat))
            cloudSettings_.mode = CloudMode::Flat;
        if (ui_.button(ui, kCloudMode + 1, puffy.x, puffy.y, puffy.width, puffy.height,
                       text(cloudModeName(CloudMode::Puffy)),
                       cloudSettings_.mode == CloudMode::Puffy))
            cloudSettings_.mode = CloudMode::Puffy;
        y += kShapeRowHeight + 4.0f;
    }

    // --- the shape ----------------------------------------------------------
    // Two shapes in a row: the names are words and words do not fit in a
    // fifth of a panel.
    ui.textFit(x, y, width, text("cloud.shape"), theme::textDim);
    y += theme::textLineHeight + 4.0f;

    constexpr int kShapesPerRow = 2;
    const float shapeWidth = (width - 4.0f) / (float)kShapesPerRow;
    for (int i = 0; i < kCloudShapeCount; ++i) {
        const int col = i % kShapesPerRow;
        const int row = i / kShapesPerRow;
        const Rect hit{x + (float)col * (shapeWidth + 4.0f),
                       y + (float)row * (kShapeRowHeight + 4.0f), shapeWidth, kShapeRowHeight};
        const bool on = (int)cloudSettings_.shape == i;
        if (ui_.button(ui, kCloudShape + i, hit.x, hit.y, hit.width, hit.height,
                       text(cloudShapeName((CloudShape)i)), on))
            cloudSettings_.shape = (CloudShape)i;
    }
    y += (float)((kCloudShapeCount + kShapesPerRow - 1) / kShapesPerRow) *
             (kShapeRowHeight + 4.0f) +
         2.0f;

    // --- the puffs ----------------------------------------------------------
    // How many of them there are, how big they are, and how much sky the
    // cloud takes. The last one is the difference between a cloud and a
    // wisp of one.
    ui_.propertyInt(ui, kCloudPuffs, x, y, width, text("cloud.puffs"),
                    cloudSettings_.puffs, 1, 512, 0.25f);
    y += kFieldHeight;
    ui_.propertyFloat(ui, kCloudPuffSize, x, y, width, text("cloud.puffSize"),
                      cloudSettings_.puffSize, 0.05f, 0.2f, 40.0f);
    y += kFieldHeight;
    ui_.propertyFloat(ui, kCloudSpread, x, y, width, text("cloud.spread"),
                      cloudSettings_.spread, 0.5f, 2.0f, 400.0f);
    y += kFieldHeight;
    ui_.propertyFloat(ui, kCloudHeight, x, y, width, text("cloud.height"),
                      cloudSettings_.height, 0.25f, 1.0f, 200.0f);
    y += kFieldHeight;
    ui_.propertyFloat(ui, kCloudCoverage, x, y, width, text("cloud.coverage"),
                      cloudSettings_.coverage, 0.005f, 0.0f, 1.0f);
    y += kFieldHeight;
    ui_.propertyFloat(ui, kCloudSoftness, x, y, width, text("cloud.softness"),
                      cloudSettings_.softness, 0.005f, 0.0f, 1.0f);
    y += kFieldHeight;

    // --- the cost of it -----------------------------------------------------
    // The detail of the noise, and how finely a puff is made. The second one
    // is the number the whole cost of the cloud is made of: a cloud is
    // hundreds of puffs, and every one of them is a ball of triangles.
    ui_.propertyInt(ui, kCloudDetail, x, y, width, text("cloud.detail"),
                    cloudSettings_.detail, 1, 8, 0.25f);
    y += kFieldHeight;
    ui_.propertyInt(ui, kCloudResolution, x, y, width, text("cloud.resolution"),
                    cloudSettings_.resolution, 3, 32, 0.25f);
    y += kFieldHeight;

    // The seed and the button that gives a new one. The same seed is the same
    // cloud everywhere, so the number is worth showing and worth keeping.
    ui_.propertyInt(ui, kCloudSeed, x, y, width - 40.0f, text("cloud.seed"),
                    cloudSeed_, 0, 999999, 1.0f);
    {
        const Rect dice{x + width - 36.0f, y, 36.0f, theme::fieldHeight};
        if (ui_.button(ui, kCloudRandom, dice.x, dice.y, dice.width, dice.height, "?"))
            cloudSeed_ = nextSeed(cloudSeed_);
    }
    y += kFieldHeight;

    // --- the colours --------------------------------------------------------
    ui.textFit(x, y, width, text("cloud.color"), theme::textDim);
    y += theme::textLineHeight + 4.0f;
    {
        const float swatch = std::min(28.0f, (width - 10.0f) / (float)kCloudPalettes);
        const int chosen = paletteOf(cloudSettings_);
        for (int i = 0; i < kCloudPalettes; ++i) {
            const float sx = x + (float)i * (swatch + 2.0f);
            const Rect hit{sx, y, swatch, swatch};
            if (ui_.button(ui, kCloudPalette + i, hit.x, hit.y, hit.width, hit.height, "")) {
                cloudSettings_.color[0] = kCloudPaletteValues[i].lit[0];
                cloudSettings_.color[1] = kCloudPaletteValues[i].lit[1];
                cloudSettings_.color[2] = kCloudPaletteValues[i].lit[2];
                cloudSettings_.shade[0] = kCloudPaletteValues[i].shade[0];
                cloudSettings_.shade[1] = kCloudPaletteValues[i].shade[1];
                cloudSettings_.shade[2] = kCloudPaletteValues[i].shade[2];
            }
            // The swatch shows the two halves of the pair: the colour the
            // sun reaches above, the colour of the shadow below.
            ui.fillRect(hit.x, hit.y, hit.width, hit.height * 0.5f,
                        Color{kCloudPaletteValues[i].lit[0], kCloudPaletteValues[i].lit[1],
                              kCloudPaletteValues[i].lit[2]});
            ui.fillRect(hit.x, hit.y + hit.height * 0.5f, hit.width, hit.height * 0.5f,
                        Color{kCloudPaletteValues[i].shade[0], kCloudPaletteValues[i].shade[1],
                              kCloudPaletteValues[i].shade[2]});
            // The one that is chosen is the one with a frame around it.
            if (i == chosen) {
                ui.fillRect(hit.x - 2.0f, hit.y - 2.0f, hit.width + 4.0f, 1.0f, theme::accent);
                ui.fillRect(hit.x - 2.0f, hit.y + hit.height + 1.0f, hit.width + 4.0f, 1.0f,
                            theme::accent);
            }
        }
        y += swatch + 8.0f;
    }

    // How tall the panel wanted to be, measured on the way out.
    cloudContentHeight_ = y + cloudScroll_ - body.y + 8.0f;

    ui_.popClip();
    ui.popClip();

    // --- the bar ------------------------------------------------------------
    // The button and the hint, at the bottom of the panel, out of the way of
    // the scroll: the button that makes the cloud is always in sight and
    // always where it can be pressed.
    const float barY = dock.y + dock.height - barHeight;
    ui.fillRect(dock.x, barY, dock.width, 1.0f, theme::separator);
    const Rect button{dock.x + 8.0f, barY + 6.0f, dock.width - 16.0f, kButtonHeight};
    const bool generate = ui_.button(ui, kCloudGenerate, button.x, button.y, button.width,
                                     button.height, text("cloud.generate"));
    if (generate) {
        makeCloud(world);
        // And the seed moves on, so that the next cloud is a different one.
        cloudSeed_ = nextSeed(cloudSeed_);
    }
    ui.textFit(dock.x + 8.0f, button.y + kButtonHeight + 4.0f, dock.width - 16.0f,
               text("cloud.hint"), theme::textDim);
}

void Editor::makeCloud(PhysicsWorld& world) {
    // The cloud's own copy of the settings, with the numbers the fields kept,
    // and the threads the user chose in the settings.
    CloudSettings settings = cloudSettings_;
    settings.seed = (unsigned)cloudSeed_;
    settings.threads = loaderThreads_;

    // The cloud itself. Whatever was being changed before is finished first:
    // one change is one entry on the undo stack.
    commitUndo();
    cloud_.build(settings);

    MeshAsset asset = cloud_.toMesh();
    if (asset.parts.empty()) {
        Log::error("Cloud: the cloud could not be made");
        return;
    }
    // The name of the mesh is the seed, and it has to be: the registry hands
    // back the mesh it already has under a name, so a cloud that is called
    // "cloud" twice is the same cloud twice - the second one would be the
    // first one with a different name on it, and the button would look like
    // it did nothing.
    asset.name = "cloud " + std::to_string(settings.seed);
    // The settings the cloud was made of travel with the mesh, so a scene
    // that is saved says how the cloud was made and the cloud is made again
    // when the scene is opened.
    asset.cloud = cloud_.settings();

    const int mesh = meshes_.importMesh(std::move(asset));
    if (mesh < 0) {
        Log::error("Cloud: the cloud could not be put in the registry");
        return;
    }

    // The body it becomes: a mesh body, with the size the mesh says it has.
    const MeshAsset* made = meshes_.at(mesh);
    RigidBody body;
    body.shape = Shape::Mesh;
    body.mesh = mesh;
    body.halfExtents = made ? made->half : Vec3{1.0f, 1.0f, 1.0f};
    // The body stands where the middle of the cloud is, so that the cloud is
    // drawn exactly where it was made: the bottom of it is on the ground of
    // the world and not half a height below it.
    body.position = made ? made->center : Vec3{0.0f, 0.0f, 0.0f};
    // A cloud does not fall.
    body.material.isStatic = true;
    body.configure(body.mass);
    body.syncPrevTransform();
    world.bodies.push_back(body);
    world.wakeAll();
    // Remembered, so that Ctrl+Z can take the cloud away again.
    rememberAddition(world, (int)world.bodies.size() - 1);
    commitUndo();

    // And it is selected, so the inspector shows what was just made.
    activeBody_ = (int)world.bodies.size() - 1;
    selectedBodies_.assign(1, activeBody_);
    markSceneDirty();

    const MeshAsset* after = meshes_.at(mesh);
    const int triangles = after ? (int)(after->parts[0].indices.size() / 3) : 0;
    Log::info("Cloud: %d puffs, %d triangles, %s", cloud_.puffCount(), triangles,
              settings.mode == CloudMode::Flat ? "flat" : "a volume");
}

} // namespace s3d
