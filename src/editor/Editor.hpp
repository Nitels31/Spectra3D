#pragma once
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "../assets/MeshRegistry.hpp"
#include "../i18n/Localizer.hpp"
#include "../physics/PhysicsWorld.hpp"
#include "../render/Camera.hpp"
#include "../render/SceneRenderer.hpp"
#include "../render/UiRenderer.hpp"
#include "../ui/UiContext.hpp"
#include "AssetBrowser.hpp"
#include "Gizmo.hpp"

namespace s3d {

// Panel geometry, recomputed once per frame from the framebuffer size. Panels
// read these instead of recomputing offsets.
struct EditorLayout {
    Rect menuBar;
    Rect leftDock;
    Rect rightDock;
    Rect viewport;
    Rect viewportChrome; // toolbar strip inside the viewport
    Rect statusBar;
};

// Godot-style editor shell. Owns selection, play mode and the UI context, and
// delegates drawing to the panels in `panels/`.
class Editor {
public:
    // Widget id ranges, kept disjoint so panels can never collide.
    enum : int {
        kMenuBar = 100,
        kSceneDockTabs = 200,
        kCreateButton = 210,
        kAssetDock = 300,
        kImportRow = 310,
        kHierarchyRow = 400,
        kInspectorDock = 500,
        kInspectorProperty = 600,
        kFilterField = 700,
        kViewportChrome = 800,
        kStatusBar = 900,
    };

    Editor(MeshRegistry& meshes, i18n::Localizer& localizer);

    // --- per frame ----------------------------------------------------------
    void handleInput(PhysicsWorld& world, Camera& camera, Window& window, int fbWidth,
                     int fbHeight);
    void renderOverlay(SceneRenderer& scene, PhysicsWorld& world);
    void renderUi(UiRenderer& ui, PhysicsWorld& world, Camera& camera, Window& window,
                  int fbWidth, int fbHeight);

    // --- simulation control (read by the application) -----------------------
    bool simulating() const { return playMode_ && playing_; }
    bool consumeSingleStep();
    bool consumeTimeReset() { return std::exchange(timeResetRequested_, false); }
    // True once, on the frame the editor enters play mode. The application runs
    // the project's scripts at that point, so they see a live world.
    bool consumePlayStarted() { return std::exchange(playStarted_, false); }

    // Scene file this editor saves to and loads from. Empty falls back to
    // "scene.txt" next to the binary, which is what happens when the editor runs
    // without a project.
    void setScenePath(std::string path) { scenePath_ = std::move(path); }
    const char* scenePath() const {
        return scenePath_.empty() ? "scene.txt" : scenePath_.c_str();
    }

    bool showVectors() const { return showVectors_; }
    bool showGrid() const { return showGrid_; }
    const EditorLayout& layout() const { return layout_; }

    void saveScene(const PhysicsWorld& world);
    void loadScene(PhysicsWorld& world);

private:
    // --- input phases -------------------------------------------------------
    void handleHotkeys(PhysicsWorld& world, Window& window);
    void handleViewport(PhysicsWorld& world, Camera& camera, Window& window);
    bool tryGizmoGrip(PhysicsWorld& world, Camera& camera);
    void applyGizmoDrag(PhysicsWorld& world, Camera& camera);
    bool tryNavigationGizmo(Camera& camera);
    void handleSelectionClick(PhysicsWorld& world, const Vec3& rayOrigin,
                              const Vec3& rayDir);
    void enterPlayMode(PhysicsWorld& world);
    void exitPlayMode(PhysicsWorld& world);

    void recomputeLayout(int fbWidth, int fbHeight);
    void pruneSelection(const PhysicsWorld& world);
    void deleteSelection(PhysicsWorld& world);

    // Projection basis for the current pointer position inside the viewport.
    // `camera.basis()` costs four transcendentals and the field of view one
    // more, and both the picking ray and the gizmo projection want the same
    // values, so they are derived at most once per input frame. Rebuilt lazily:
    // a frame where the pointer never reaches the viewport never pays for it.
    struct ViewRay {
        Vec3 forward, right, up;
        float tanX = 0.0f, tanY = 0.0f;
        bool valid = false;
    };
    void updateViewRay(const Camera& camera);
    Vec3 viewportRay(const Camera& camera);

    // snprintf replacements for the small "%s" / "%02d" labels the panels build
    // every frame. Each has a memcpy fast path and falls back to the original
    // snprintf call whenever the text would not fit, so truncation is unchanged.
    static void appendDecimal(char* out, int& at, unsigned long long value, int minDigits);
    static void formatIndexed(char* out, size_t size, const char* name, char separator,
                              int index);
    static void formatPrefixed(char* out, size_t size, const char* prefix, const char* value);

    // --- panels (defined in panels/*.cpp) -----------------------------------
    void drawMenuBar(UiRenderer& ui, PhysicsWorld& world, int fbWidth);
    void drawSceneDock(UiRenderer& ui, PhysicsWorld& world, Camera& camera, int fbHeight);
    void drawHierarchy(UiRenderer& ui, PhysicsWorld& world, Camera& camera, const Rect& dock);
    void drawAssetDock(UiRenderer& ui, PhysicsWorld& world, Camera& camera, const Rect& dock);
    void drawInspectorDock(UiRenderer& ui, PhysicsWorld& world, Window& window, int fbWidth,
                           int fbHeight);
    void drawEmptyInspectorCard(UiRenderer& ui, const Rect& dock, float y);
    void drawTransformSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y);
    void drawShapeSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y);
    void drawMaterialSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y);
    void drawFlagsSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y);
    void drawAnchorSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y);
    void drawStateSection(UiRenderer& ui, PhysicsWorld& world, RigidBody& body,
                          const Rect& dock, float& y);
    void drawViewportChrome(UiRenderer& ui, Camera& camera);
    void drawStatusBar(UiRenderer& ui, PhysicsWorld& world, int fbWidth, int fbHeight);
    void drawConsolePanel(UiRenderer& ui, PhysicsWorld& world, int fbWidth);
    void drawNavigationBalls(UiRenderer& ui) const;

    MeshRegistry& meshes_;
    i18n::Localizer& loc_;
    UiContext ui_;
    AssetBrowser assets_;

    // --- selection ----------------------------------------------------------
    int activeBody_ = -1;
    std::vector<int> selectedBodies_;

    // --- play mode ----------------------------------------------------------
    bool playMode_ = false;
    bool playing_ = true;
    bool singleStepRequested_ = false;
    bool timeResetRequested_ = false;
    bool playStarted_ = false;
    std::string scenePath_;
    std::vector<RigidBody> editSnapshot_;
    GravityAxis snapshotGravity_ = GravityAxis::NegY;

    // --- viewport options ---------------------------------------------------
    bool gizmoEnabled_ = true;
    gizmo::Mode gizmoMode_ = gizmo::Mode::Move;
    bool showGrid_ = true;
    bool showVectors_ = true;
    int perspectiveIndex_ = 1;
    bool showConsole_ = false;
    int sceneTab_ = 0;
    int inspectorTab_ = 0;
    float hierarchyScroll_ = 0.0f;
    TextField filter_;
    EditorLayout layout_;

    // --- gizmo drag ---------------------------------------------------------
    int dragAxis_ = -1;
    Vec3 dragOrigin_;
    float dragStartAxisT_ = 0.0f;
    float dragStartScale_ = 1.0f;

    ViewRay viewRay_;

    const char* text(const char* key) const { return loc_.get(key); }
};

} // namespace s3d