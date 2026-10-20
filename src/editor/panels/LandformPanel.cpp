#include "editor/Editor.hpp"

#include "core/Log.hpp"
#include "core/Math.hpp"
#include "scene/Landform.hpp"
#include "ui/Theme.hpp"

namespace s3d {
namespace {

constexpr float kHeaderHeight = 24.0f;
constexpr float kFieldHeight = 24.0f;
constexpr float kButtonHeight = 26.0f;

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

    float y = dock.y + kHeaderHeight + 10.0f;
    const float width = dock.width - 16.0f;
    const float x = dock.x + 8.0f;

    // The heights the ground goes between, and the size of it. The fields drag
    // their own numbers, like every other number in the editor.
    ui_.propertyFloat(ui, kLandformMinHeight, x, y, width, text("landform.minHeight"),
                      landform_.minHeight, 0.05f, -1000.0f, 1000.0f);
    y += kFieldHeight;
    ui_.propertyFloat(ui, kLandformMaxHeight, x, y, width, text("landform.maxHeight"),
                      landform_.maxHeight, 0.05f, -1000.0f, 1000.0f);
    y += kFieldHeight;
    ui_.propertyFloat(ui, kLandformWidth, x, y, width, text("landform.width"), landform_.width,
                      0.1f, 1.0f, 1000.0f);
    y += kFieldHeight;
    ui_.propertyFloat(ui, kLandformDepth, x, y, width, text("landform.depth"), landform_.depth,
                      0.1f, 1.0f, 1000.0f);
    y += kFieldHeight;
    ui_.propertyFloat(ui, kLandformCells, x, y, width, text("landform.cells"),
                      landformCells_, 1.0f, 2.0f, 512.0f);
    y += kFieldHeight;

    // The button, and under it what the button is going to do.
    const Rect button{x, y, width, kButtonHeight};
    if (ui_.button(ui, kLandformGenerate, button.x, button.y, button.width, button.height,
                   text("landform.generate")))
        makeLandform(world);
    y += kButtonHeight + 6.0f;

    ui.textFit(x, y, width, text("landform.hint"), theme::textDim);
}

void Editor::makeLandform(PhysicsWorld& world) {
    // The landform's own copy of the settings, with the number of cells as the
    // field kept it, and the threads the user chose in the settings.
    LandformSettings settings = landform_;
    settings.cells = (int)landformCells_;
    settings.threads = loaderThreads_;

    MeshAsset asset = generateLandform(settings);
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
    body.configure(body.mass);
    body.syncPrevTransform();
    world.bodies.push_back(body);
    world.wakeAll();

    // And it is selected, so the inspector shows what was just made.
    activeBody_ = (int)world.bodies.size() - 1;
    selectedBodies_.assign(1, activeBody_);
    markSceneDirty();
    Log::info("Landform: %dx%d ground made, %d squares, %d thread(s)", (int)settings.width,
              (int)settings.depth, settings.cells * settings.cells, settings.threads);
}

} // namespace s3d
