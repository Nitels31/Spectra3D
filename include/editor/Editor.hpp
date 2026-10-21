#pragma once
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "assets/MeshRegistry.hpp"
#include "audio/SoundDevice.hpp"
#include "i18n/Localizer.hpp"
#include "physics/PhysicsWorld.hpp"
#include "render/Camera.hpp"
#include "render/SceneRenderer.hpp"
#include "render/UiRenderer.hpp"
#include "ui/UiContext.hpp"
#include "editor/AssetBrowser.hpp"
#include "editor/Gizmo.hpp"
#include "scene/Landform.hpp"
#include "editor/Gizmo.hpp"
#include "scene/Landform.hpp"
#include "editor/PanelLayout.hpp"

namespace s3d {


// Panels that can be dragged between the edges and the middle of the window, the
// way Unity's are. Four of them, and five places to be: the two vertical edges, the
// bottom strip, the two bottom corners and free float over the scene.
//
// The corners are separate zones rather than "the left edge, but lower", because a
// panel that owns the bottom-left corner and a panel that owns the left edge both
// exist at once - the corner is where the console stops covering the hierarchy, and
// without its own zone there is nowhere to put a second list once the first one has
// the bottom strip.
//
// `Float` is last on purpose: several tables are sized `(int)PanelZone::Float` and
// index them by zone.
enum class PanelId {
    Hierarchy = 0,
    Inspector = 1,
    Console = 2,
    Favorites,
    Sounds,
    Landform,
    PanelCount
};
// Where a panel can be, and the geometry that follows from it, live in
// PanelLayout.hpp: the zones, the workspace as numbers, and the rectangles they
// make up. They are there and not here because a test has to be able to ask the
// same questions the editor asks - with no window and no editor.


// Whether two bodies are the same body in the same state, as far as the
// editor is concerned.
//
// The inspector edits a body in place, so the only way to know that a number
// was changed is to compare the body before and after. The fields that the
// simulation writes every frame - the velocity, the sleep timer - are not
// among them: they change on their own, and a change the user did not make is
// not a change that belongs on the undo stack.
bool sameBodyState(const RigidBody& a, const RigidBody& b);

// Godot-style editor shell. Owns selection, play mode and the UI context, and
// delegates drawing to the panels in `panels/`.
class Editor {
public:
    // Widget id ranges, kept disjoint so panels can never collide.
    //
    // The number space is cut into stretches, and every stretch says what it
    // is for: a shared id is two widgets in two corners of the window taking
    // the press away from each other, and the press lands in the wrong place.
    // A test asks the stretches whether they still fit.
    enum : int {
        // 100 - 199: the top bar.
        kTopBar = 100,
        kTransportPlay = 101,
        kTransportPause = 102,
        kTransportStop = 103,
        kTopBarTool = 120,  // + the tool index, the five tools in the bar itself

        // 200 - 299: the dropdowns of the bar.
        kMenuRow = 200,  // + row index

        // 300 - 399: the toolbar, the overlays that hang off it, and the
        // chrome of the panels.
        kToolbarHelp = 300,
        kToolbarLayers = 301,
        kToolbarReload = 302,
        kToolbarSearch = 303,
        kToolbarCounter = 304,
        kLayerRow = 320,  // + body index
        kLayerToggle = 340,
        kHelpClose = 350,
        kPanelGrip = 360,
        // Not a widget: the id the popup capture is claimed under. Any non-zero
        // value does, this one only has to be unlikely.
        kPopupCapture = 380,

        // 400 - 999: the hierarchy, one row per body. Six hundred bodies is
        // more than a scene holds, and the stretch is that wide on purpose.
        kHierarchyRow = 400,  // + body index
        kHierarchyStar = 900,
        kHierarchyFavOnly = 901,
        kHierarchySearch = 902,

        // 1000 - 1099: the favourites, one row per starred body.
        kFavoriteRow = 1000,  // + index

        // 1100 - 1199: the sounds, one row per sound.
        kSoundRow = 1100,  // + index

        // 1200 - 1299: the landform.
        kLandformMinHeight = 1200,
        kLandformMaxHeight = 1201,
        kLandformWidth = 1202,
        kLandformDepth = 1203,
        kLandformCells = 1204,
        kLandformGenerate = 1205,
        kLandformColor = 1210,  // + the swatch
        kLandformShape = 1220,  // + the shape
        kLandformSeed = 1230,
        kLandformOctaves = 1231,
        kLandformFrequency = 1232,
        kLandformWarp = 1233,
        kLandformRidge = 1234,
        kLandformErosion = 1235,
        kLandformSmoothing = 1236,
        kLandformBiomes = 1237,
        kLandformRandom = 1238,
        // The numbers the shapes are made of, and the one that lets a ground
        // differ from the ground that was made before it.
        kLandformLevels = 1240,
        kLandformRoundness = 1241,
        kLandformCut = 1242,
        kLandformRiver = 1243,
        kLandformMeander = 1244,
        kLandformShore = 1245,
        kLandformLowland = 1246,
        kLandformStretch = 1247,
        kLandformVariation = 1248,
        kLandformGround = 1249,

        // 1300 - 1399: the inspector.
        kInspectorDock = 1300,
        kInspectorProperty = 1310,  // + the property

        // 1400 - 1499: the fields that filter something.
        kFilterField = 1400,
        kConsoleFilter = 1401,

        // 1500 - 1599: the strip above the scene.
        kSceneHeader = 1500,

        // 1600 - 1699: the shadow settings dialog.
        //
        // The sun and the two numbers that decide how a shadow is compared
        // with the map. They are the project's and not the machine's, and
        // they are set where the shadows can be seen.
        kShadowAzimuth = 1600,
        kShadowElevation = 1601,
        kShadowBias = 1602,
        kShadowNormal = 1603,
        kShadowRadius = 1604,
    };

    // Who is drawing, for the overlay guard. The surfaces a pass puts down do
    // not take the press from the widgets of that same pass - otherwise a
    // panel would take the press from its own Close button.
    enum : int {
        kOwnerTopBarOverlay = 1700,
        kOwnerModal = 1701,
        kOwnerFloatingPanel = 1710,  // + the panel
    };

    Editor(MeshRegistry& meshes, i18n::Localizer& localizer, SoundDevice& sound);

    // --- per frame ----------------------------------------------------------
    void handleInput(PhysicsWorld& world, Camera& camera, Window& window, int fbWidth,
                     int fbHeight, float dt);
    void renderOverlay(SceneRenderer& scene, PhysicsWorld& world, int hiddenBody = -1);
    void renderUi(UiRenderer& ui, PhysicsWorld& world, Camera& camera, Window& window,
                  int fbWidth, int fbHeight);

    // --- simulation control (read by the application) -----------------------
    bool simulating() const { return playMode_ && playing_; }
    bool consumeSingleStep();
    bool consumeTimeReset() { return std::exchange(timeResetRequested_, false); }
    // True once, on the frame the editor enters play mode. The application runs
    // the project's scripts at that point, so they see a live world in the first
    // Play frame. True once more, on the frame play mode ends, so the shell can
    // run a script's onStop() - the editor cannot call it itself, because the
    // script host belongs to the application and the dependency only points up.
    bool consumePlayStarted() { return std::exchange(playStarted_, false); }
    bool consumePlayStopped() { return std::exchange(playStopped_, false); }

    // Scene file this editor saves to and loads from. Empty falls back to
    // "scenes/scene.json" next to the binary, which is what happens when the
    // editor runs without a project.
    void setScenePath(std::string path) { scenePath_ = std::move(path); }
    // Shown in the window title, the way the sketch has it: engine, dash, project.
    void setProjectName(std::string name) { projectName_ = std::move(name); }
    // The camera the project was opened with. "Reset camera" goes back to it
    // rather than to a hardcoded default, so a project that was saved looking at
    // something gets that something back.
    void setHomeCamera(const Camera& camera) { homeCamera_ = camera; }

    const char* scenePath() const {
        return scenePath_.empty() ? "scenes/scene.json" : scenePath_.c_str();
    }

    // Saved the scene since it was last loaded or written. New Scene, Open Scene
    // and New Project all ask about it first, because there is no autosave and
    // nothing else stands between a stray click and an afternoon of work.
    bool sceneDirty() const { return sceneDirty_; }
    // The application owns gravity (it sets it from the manifest), so a keyboard
    // axis change there has to be able to say "this is an edit".
    void markSceneDirty() { sceneDirty_ = true; }
    // How many threads the loader and the landform may use.
    void setLoaderThreads(int threads) { loaderThreads_ = threads < 1 ? 1 : (threads > 6 ? 6 : threads); }
    // The application owns the launcher, the manifest and the build, so the editor
    // asks for those through this one-shot request instead of reaching for them.
    enum class ShellRequest { None, NewProject, OpenProject, SaveProject, Build, BuildRun };
    ShellRequest consumeShellRequest() {
        return std::exchange(shellRequest_, ShellRequest::None);
    }
    // Where Build Settings says the build should go. Staging it is not the
    // editor's business, so it only reports the folder and asks.
    const std::string& buildFolder() const { return buildFolder_; }

    bool showVectors() const { return showVectors_; }
    bool showGrid() const { return showGrid_; }
    // The map of the shadows, drawn over the scene.
    bool showShadowMap() const { return showShadowMap_; }
    const LayoutRects& layout() const { return layout_; }

    // The shadows, as the user sets them. The settings belong to the project
    // and not to the machine, and the editor changes them where the shadows
    // can be seen: the pointer is the application's copy, and the application
    // puts it into the renderer and into the manifest.
    void setShadowSettings(ShadowSettings* settings) { shadowSettings_ = settings; }

    void saveScene(const PhysicsWorld& world);
    void loadScene(PhysicsWorld& world);

    // Splits a text into lines that are no wider than `maxWidth`, on word
    // boundaries. Used by the panels whose text has to fit a narrow column: a
    // sentence that runs out of its box is a sentence nobody reads to the end.
    static void wrapText(const UiRenderer& ui, float maxWidth, const char* utf8,
                         std::vector<std::string>& out);

private:


    // --- input phases -------------------------------------------------------
    void handleHotkeys(PhysicsWorld& world, Camera& camera, Window& window, float dt);
    void handleViewport(PhysicsWorld& world, Camera& camera, Window& window);
        bool tryGizmoGrip(PhysicsWorld& world, Camera& camera);
    // The pointer's angle around a ring, in screen space, measured about the ring's
    // projected centre. False when there is no answer this frame - the centre is
    // behind the camera, or the pointer is standing on it - which the caller reads
    // as "no movement" and never as "no angle".
    bool ringScreenAngle(const Vec3& centre, const Camera& camera, float& angleOut) const;
    void applyGizmoDrag(PhysicsWorld& world, Camera& camera);
    bool tryNavigationGizmo(Camera& camera);
    void handleSelectionClick(PhysicsWorld& world, const Vec3& rayOrigin,
                              const Vec3& rayDir);
    void enterPlayMode(PhysicsWorld& world);
    void exitPlayMode(PhysicsWorld& world);

    void recomputeLayout(int fbWidth, int fbHeight);
    void pruneSelection(const PhysicsWorld& world);
    void deleteSelection(PhysicsWorld& world);

    // --- the dialogs Unity's File menu hangs off ---------------------------
    // One modal at a time, drawn centred over the scene like the help pages. Each
    // is a real command with a real result, not a message box that only says "ok".
    enum class Modal { None, NewScene, OpenScene, BuildSettings, SaveFirst, ShadowSettings };
    Modal modal_ = Modal::None;
    // What a pending "save first?" is about to do once the answer is known.
    enum class Pending { NewScene, OpenScene, LeaveProject };
    Pending pendingAction_ = Pending::NewScene;
    // Which way out was asked for when the answer to "save first?" arrives. Both
    // New Project and Open Project leave for the launcher, but they are different
    // commands and only one of them can be told apart again afterwards.
    ShellRequest pendingShell_ = ShellRequest::None;
    TextField modalField_;
    // Scene files offered by the Open Scene dialog, snapshotted when it opens so
    // the list cannot change under the click that is reading it.
    std::vector<std::string> modalFiles_;
    int modalRow_ = 0;

    bool sceneDirty_ = false;
    std::string buildFolder_;
    ShellRequest shellRequest_ = ShellRequest::None;

    // Framebuffer size of the last layout pass: the drag code asks "is the pointer
    // near the left edge" and needs the window, not the panels.
    int fbWidth_ = 0, fbHeight_ = 0;
    // Dock sizes. Seeded from the theme metrics, then the user's: dragging an edge
    // is the only way to change them, and the value survives everything else -
    // and the next launch, because they are part of the saved workspace.
    //
    // The bottom row is three panels and not one row split in three, so it has
    // three heights: the console, the favorites in the left corner and the
    // sounds in the right one are three panels that happen to stand next to
    // each other, and one of them must not be dragged taller because another one
    // was.
    float leftWidth_ = 260.0f;
    float rightWidth_ = 280.0f;
    float bottomHeight_ = 156.0f;
    float bottomLeftHeight_ = 156.0f;
    float bottomRightHeight_ = 156.0f;

    // Which edges of a panel a resize grip the pointer took: one bit per edge, so
    // a corner is two bits and the drag itself is four independent ifs. A docked
    // panel only ever has the one edge that faces the scene; a floating one has
    // all four.
    enum : unsigned {
        kGripLeft = 1u,
        kGripRight = 2u,
        kGripTop = 4u,
        kGripBottom = 8u,
    };

    // Drag in progress. -1 when nothing is being dragged.
    int dragPanel_ = -1;
    int resizePanel_ = -1;
    unsigned resizeEdges_ = 0;
    PanelZone dropZone_ = PanelZone::Left;
    Rect dragRect_{};
    float dragGrabX_ = 0.0f, dragGrabY_ = 0.0f;
    float resizeStartPointer_ = 0.0f;
    float resizeStartSize_ = 0.0f;
    // A floating panel is resized from where it was when the grip was taken, not
    // from where it is this frame: the pointer's own position is the delta.
    Rect resizeStartRect_{};
    float resizeStartPointerX_ = 0.0f, resizeStartPointerY_ = 0.0f;

    // The workspace as the user left it. Written out once per change (see
    // EditorLayout.cpp) and read back on the first frame, so a project opens the
    // way it was closed rather than the way it was first seen.
    bool layoutLoaded_ = false;
    bool layoutDirty_ = false;

    // A press the editor acts on: one that follows a release, and nothing else.
    //
    // The button can already be down when the editor appears - the click that
    // opened the project is the launcher's, and the mouse is still held - and a
    // press there is the second half of somebody else's click. It used to pick a
    // panel up and drag it to the end of its track, and then write the
    // workspace out with the panel at its smallest: the user's own layout,
    // changed by the editor, with nothing to have done with it.
    bool pointerArmed_ = false;
    bool pressAccepted() const { return pointerArmed_ && ui_.pointerPressed(); }

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

    // --- the landform --------------------------------------------------------
    // What the Landform panel says: how low and how high the ground goes, how
    // big it is, how finely it is cut, and what the noise is made of. The
    // cells are a whole number, so they are kept apart from the rest.
    LandformSettings landformSettings_;
    float landformCells_ = 96.0f;
    // The seed of the noise, kept as a whole number because that is what a
    // field can show and what a person can read out loud.
    int landformSeed_ = 1;
    // How far the landform panel is scrolled, and how tall its content turned
    // out to be. The second number is measured, not predicted.
    float landformScroll_ = 0.0f;
    float landformContentHeight_ = 0.0f;
    // The ground that was made last, and the heightfield it was made from.
    // The editor asks it how high the ground is at a point, so a body dropped
    // on it lands on it and not near it.
    Landform landform_;
    // How many threads the making may use. The user picks the number in the
    // settings, and the panel uses it.
    int loaderThreads_ = 4;

    // --- hierarchy -----------------------------------------------------------
    // The label a row shows is built the same way for the row and for the filter,
    // so the filter can never disagree with what is on screen. Neither allocates:
    // the hierarchy redraws every frame and the body count is unbounded.
    const char* bodyKind(const PhysicsWorld& world, int index) const;
    void rebuildFilter(const PhysicsWorld& world, const char* query);
    // A five-pointed star from small squares along five rays. There is no glyph for
    // a star in the atlas and no rotated rectangle in the batcher, so this is what
    // "favourite" looks like.
    static void drawStar(UiRenderer& ui, float cx, float cy, float radius, const Color& tint);
    // The camera's row icon: a triangle pointing right, the way a camera is
    // drawn everywhere else.
    static void drawCameraIcon(UiRenderer& ui, float x, float y, float size,
                                const Color& tint);
    // Body indexes that pass the current filter. A member so its capacity survives
    // between frames; the obvious alternative - walk the bodies again for every
    // visible row - is quadratic.
    std::vector<int> filterRows_;
    // The same for the Favorites panel: the starred bodies, in scene order. A
    // separate list because it is a different question - "which ones do I care
    // about" rather than "which ones match what I typed".
    std::vector<int> favoriteRows_;
    float favoritesScroll_ = 0.0f;
    // "Only favourites", the star in the hierarchy header. With it on, the filter
    // is not the only thing that decides what a row shows.
    bool favoritesOnly_ = false;

    // The toolbar's tools. Every one changes what a drag in the viewport does -
    // there is no tool here that only renames another one.
    enum class Tool { Select = 0, Move, Rotate, Scale, Pan };
    Tool tool() const { return tool_; }
    void setTool(Tool tool) { tool_ = tool; }

    // The gizmo mode a tool drags with, or false when the tool shows no gizmo.
    bool gizmoModeFor(Tool tool, gizmo::Mode& modeOut) const;

    // --- toolbar (panels/TopBar.cpp, panels/TopBarOverlay.cpp) -------------
    // What a menu row does. One enum for every menu, so a row is a value plus a
    // parameter and applying it is a single switch in one place.
    enum class Action {
        None,
        // File, in the order Unity lists them.
        NewScene,
        OpenScene,
        Save,
        SaveAs,
        NewProject,
        OpenProject,
        SaveProject,
        BuildSettings,
        BuildRun,
        Exit,
        Reload,
        Duplicate,
        Delete,
        // Take the last change back. The same thing the Ctrl+Z key does, and
        // it is in the menu because the menu is where everybody looks for it.
        Undo,
        ClearSelection,
        Wake,
        Grid,
        Vectors,
        FrameSelection,
        ResetCamera,
        Shape,
        Model,
        // A camera in the scene, placed where the editor is looking. Not part of
        // Action::Shape because the camera is not a collision shape: it has no
        // volume and is never collided.
        Camera,
        Controls,
        About,
        CloseOverlay,
        LayerSelect,
        LayerVisible,
        LayerLocked,
        // Edit: the star, both from the row and from here, because a click on a
        // 16 pixel target is not the only way a person should be able to say it.
        ToggleFavorite,
        // View: which panels are on screen at all.
        TogglePanel,
        // The map of the shadows, drawn over the scene: what the sun sees.
        ShadowMap,
        // The shadows themselves: how they are drawn, how big the map is and
        // how many stretches of the scene get a map of their own. They are the
        // project's, and they are set where they can be seen.
        ShadowQuality,
        ShadowSize,
        ShadowCascades,
        // The sun and the two numbers that decide how a shadow is compared
        // with the map. They live in a dialog of their own, because a menu
        // row cannot be dragged.
        ShadowSettings,
        // Create: a piece of ground, made of noise. It opens the panel that
        // says how the ground is made.
        Landform,
    };

    // One row of a dropdown. `text` is borrowed: every label comes either from the
    // localisation table or from the file list, and both outlive the frame.
    struct MenuRow {
        Action action = Action::None;
        int param = 0;        // shape index, model index, body index
        const char* text = nullptr;
        bool checked = false;
        bool separator = false;
    };

    enum : int { kMenuFile = 0, kMenuEdit, kMenuView, kMenuCreate, kMenuHelp, kMenuCount };

    // Every row of every menu in one array, rebuilt each frame: rows of menu m
    // are [menuFirst_[m], menuFirst_[m + 1]). A member with a fixed capacity
    // instead of a vector - the toolbar redraws on every frame and forty rows of
    // heap traffic per frame is what the rest of the UI is careful not to do.
    // The capacity is the fixed menus plus room for the Create list, which is one
    // row per model in assets/models and is the only menu whose length changes.
    void buildMenuRows();
    int menuRowCount(int menu) const {
        return menuFirst_[menu + 1] - menuFirst_[menu];
    }
    const MenuRow& menuRow(int menu, int index) const {
        return menuRows_[menuFirst_[menu] + index];
    }
    // Caption of a menu, in `kMenuFile..kMenuHelp` order. The bar and the search
    // results both need it, so the list of keys lives here rather than in one file.
    const char* menuCaption(int menu) const;

    struct MenuHit {
        int menu = 0;
        int index = 0;
    };
    // Rows whose caption contains `query`, across all five menus in menu order.
    // Writes at most `capacity` of them and returns how many matched in total, so
    // the caller can say "and 12 more" instead of truncating silently.
    int findMenuRows(const char* query, MenuHit* out, int capacity) const;

    void runAction(Action action, int param, PhysicsWorld& world, Camera& camera, Window& window);
    void closeOverlays();
    // Opens the modal for an action, or asks about unsaved work first when there is
    // any: New Scene, Open Scene and New Project all throw the current scene away.
    void requestSceneChange(Modal dialog, PhysicsWorld& world);
    void openModal(Modal dialog);
    
    // The directory a Build writes to and the name a scene file gets, both derived
    // from the open project so the defaults are right before the user types.
    std::string projectDir() const;
    // The folder the scenes of the project live in: "<project>/scenes".
    std::string scenesDir() const;
    // Points the camera at the selection, far enough back that it fits.
    void frameSelection(PhysicsWorld& world, Camera& camera);

    // The capacity is the fixed menus plus room for the Create list, which is one
    // row per model in assets/models and is the only menu whose length changes.
    // It is generous on purpose: a Create menu that silently hides the last
    // models on disk is worse than a menu that is a little too big.
    static constexpr int kMaxMenuRows = 128;
    MenuRow menuRows_[kMaxMenuRows];
    int menuFirst_[kMenuCount + 1] = {};
    // Where the search field and the layers button ended up this frame, so the
    // panels that hang off them can be anchored to exactly the same rectangles.
    Rect searchField_;
    Rect layersButton_;
    // The button the layers list was actually opened from. Two strips have one - the
    // top bar and the scene header - and a list anchored to the other strip's button
    // opens on the far side of the window from the click that asked for it.
    Rect layersAnchor_;
    // Scroll offset of an open dropdown, in pixels. A menu is as long as its
    // contents and Create lists every model on disk, so the bound on a dropdown is
    // the window, not an arbitrary row count.
    float menuScroll_ = 0.0f;

    // --- panels that can be moved (see the enum above) ----------------------
    // `floatRect` is where a panel sits while it is floating; its width and height
    // are also what it snaps back to when it is docked again, so undocking does
    // not reset a panel the user had resized.
    struct Panel {
        PanelZone zone = PanelZone::Left;
        Rect floatRect;
        // Closed panels are not drawn and take no edge. The View menu brings them
        // back: a panel that cannot be reopened is a panel that cannot be closed.
        bool open = true;
    };
    Panel panels_[kPanelCount];

    // --- panels (one file each, panels/*.cpp) -----------------------------
    void drawTopBar(UiRenderer& ui, PhysicsWorld& world, Camera& camera, Window& window,
                    int fbWidth);
    // Dropdowns and the panels hanging off the top bar, drawn last so they can
    // overlap the panels below.
    // Registers every open overlay's rectangle with the guard, in draw order and
    // without drawing anything. Runs before the panels so a click under a dropdown
    // is already suppressed when their hit tests run; drawTopBarOverlay() then
    // paints the same rectangles. Kept separate because the hit test happens in the
    // middle of the frame and the surfaces on top have to be known by then.
    void declareTopBarOverlays(UiRenderer& ui, int fbWidth, int fbHeight, int bodyCount);
    void drawTopBarOverlay(UiRenderer& ui, PhysicsWorld& world, Camera& camera, Window& window,
                           int fbWidth, int fbHeight);
    // The centred dialogs: New Scene, Open Scene, Save As, Build Settings and the
    // "unsaved work" question they all ask first.
    void drawModal(UiRenderer& ui, PhysicsWorld& world, Window& window, int fbWidth,
                   int fbHeight);
    void drawHierarchyDock(UiRenderer& ui, PhysicsWorld& world, Window& window, const Rect& dock);
    void drawInspectorDock(UiRenderer& ui, PhysicsWorld& world, Window& window, const Rect& dock);
    void drawConsolePanel(UiRenderer& ui, const Rect& panel);
    void drawFavoritesDock(UiRenderer& ui, PhysicsWorld& world, const Rect& dock);
    // The sounds the engine has loaded, and the buttons that play them and
    // give them to the body that is selected.
    void drawSoundsDock(UiRenderer& ui, PhysicsWorld& world, const Rect& dock);
    // The Landform panel: the shape of the ground, made of noise.
    void drawLandformPanel(UiRenderer& ui, PhysicsWorld& world, const Rect& dock);
    // Makes the ground the panel describes and puts it in the scene.
    void makeLandform(PhysicsWorld& world);
    // One panel, wherever it currently is: its own rectangle decides the rest.
    void drawPanelById(UiRenderer& ui, PhysicsWorld& world, Window& window, PanelId id);
    void drawDockedPanels(UiRenderer& ui, PhysicsWorld& world, Window& window);
    void drawFloatingPanels(UiRenderer& ui, PhysicsWorld& world, Window& window);
    // The floating panels, in the order they are drawn. The one under the
    // pointer is drawn last: it is the window the pointer is aiming at, and a
    // window that is under another one cannot be reached.
    int floatingPanelOrder(int* out) const;
    // The floating panels, declared into the overlay guard so that a click on
    // one of them belongs to it and not to the panels and the scene below.
    void declareFloatingPanels();
    // Title bar of a panel: the handle it is dragged by, and the strip the drop
    // preview is anchored to.
    Rect panelHeader(PanelId id) const;
    Rect panelRect(PanelId id) const;
    Rect panelCloseRect(PanelId id) const;
    void drawPanelChrome(UiRenderer& ui, PanelId id);
    void handlePanelDrag(PhysicsWorld& world);
    void drawPanelDragFeedback(UiRenderer& ui);
    const char* panelTitle(PanelId id) const;
    // True while anything that floats above the panels is up. While it is, the
    // panels stop reacting to the pointer: a dropdown is drawn after them, so their
    // hit tests have already run by the time anyone can see that they are covered.
    bool popupOpen() const;
    PanelZone zoneAt(float x, float y) const;
    // The window as numbers: the sizes the user chose, where every panel is and
    // whether it is open. The geometry is computed from this and nothing else.
    Workspace workspace() const;
    Rect zoneRect(PanelZone zone) const;
    void commitPanelDrop();
    // Resizing. `panelGripsAt` is the hit test both the drag and the highlight
    // ask, so the edge that lights up is the edge that takes the pointer.
    unsigned panelGripsAt(int index, float x, float y) const;
    void applyFloatingResize(float x, float y);
    // The saved workspace: dock sizes, where each panel lives, which are closed.
    std::string layoutPath() const;
    void loadLayout();
    void saveLayout();
    void markLayoutDirty() { layoutDirty_ = true; }
    void drawSceneHeader(UiRenderer& ui, PhysicsWorld& world, Camera& camera, Window& window);
    void drawViewportTools(UiRenderer& ui, Camera& camera);
    void drawToolIcon(UiRenderer& ui, const Rect& rect, Tool tool, bool on) const;
    void drawNavigationBalls(UiRenderer& ui) const;

    void drawEmptyInspector(UiRenderer& ui, const Rect& dock, float y);
    void drawTransformSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y);
    void drawShapeSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y);
    void drawCameraSection(UiRenderer& ui, PhysicsWorld& world, RigidBody& body,
                            const Rect& dock, float& y);
    void drawMaterialSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y);
    void drawFlagsSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y);
    // The sound the body makes when it hits something: its name, a button that
    // plays it and a button that takes it away.
    void drawSoundSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y);
    void drawAnchorSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y);
    void drawStateSection(UiRenderer& ui, PhysicsWorld& world, RigidBody& body,
                          const Rect& dock, float& y);
    void drawNodeSection(UiRenderer& ui, const Rect& dock, float& y);

    MeshRegistry& meshes_;
    i18n::Localizer& loc_;
    // The sound device, for the Sounds panel and for the inspector's sound
    // field. A reference and not a copy: the device is a thing with a thread
    // behind it, and there is exactly one of it.
    SoundDevice& sound_;
    UiContext ui_;
    AssetBrowser assets_;

    // --- selection ----------------------------------------------------------
    int activeBody_ = -1;
    std::vector<int> selectedBodies_;

    // --- undo ----------------------------------------------------------------
    // One change to one body, and how to take it back.
    //
    // The steps of one action are applied in order and taken back in the
    // reverse order, and every step knows the index it was made at - so a
    // body that was removed goes back to the place it came from, and a body
    // that was added goes away from the place it was put.
    struct UndoStep {
        enum class Kind { Changed, Added, Removed };
        Kind kind = Kind::Changed;
        int index = -1;
        RigidBody body;
    };

    // A change the user made, and everything it touched.
    struct UndoAction {
        std::vector<UndoStep> steps;
    };

    // How many changes are remembered. A hundred is more than anybody undoes
    // and less than a hundred copies of a scene.
    static constexpr int kMaxUndoActions = 100;

    std::vector<UndoAction> undoStack_;
    std::vector<UndoAction> redoStack_;
    // The change being made right now. It becomes one entry on the undo stack
    // when it is over.
    UndoAction pending_;
    // While the undo itself is running, what it does is not undone in turn.
    bool undoing_ = false;

    // The body as it was before the change that is being made now, and where
    // it was. A drag of a number is one change and not one change per frame,
    // so the state that is remembered is the one from before the first frame
    // of the drag.
    RigidBody bodyBefore_;
    int bodyBeforeIndex_ = -1;
    bool bodyBeforeValid_ = false;

    // The body as it was at the end of the last frame. It is where a change
    // made during this frame goes back to.
    RigidBody bodySnapshot_;
    int bodySnapshotIndex_ = -1;
    bool bodySnapshotValid_ = false;

    // Remembers a body as it is now, so that a change to it can be taken
    // back.
    void rememberBody(const PhysicsWorld& world, int index);
    // Remembers a body that is about to be removed.
    void rememberRemoval(const PhysicsWorld& world, int index);
    // Remembers a body that has just been added.
    void rememberAddition(const PhysicsWorld& world, int index);
    // Puts the change that was being made onto the undo stack.
    void commitUndo();
    // A change to the body that is selected, made by the inspector. The state
    // to go back to is the one from before the change began.
    void noteBodyEdited(const PhysicsWorld& world, int index);
    // Ends the change that was being made, if it is over.
    void finishUndoIfDone(const PhysicsWorld& world);
    // Takes the last change back, and puts it on the redo stack.
    void undo(PhysicsWorld& world);
    // Puts the last change back the other way.
    void redo(PhysicsWorld& world);

    // --- play mode ----------------------------------------------------------
    bool playMode_ = false;
    bool playing_ = true;
    bool singleStepRequested_ = false;
    bool timeResetRequested_ = false;
    bool playStarted_ = false;
    bool playStopped_ = false;
    std::string scenePath_;
    std::vector<RigidBody> editSnapshot_;
    GravityAxis snapshotGravity_ = GravityAxis::NegY;

    // --- viewport options ---------------------------------------------------
    bool showGrid_ = true;
    bool showVectors_ = true;
    // Snap while dragging: the position lands on a quarter of a unit instead of
    // wherever the ray happened to hit. The magnet in the viewport toolbar.
    bool snapEnabled_ = false;
    // The map of the shadows, drawn over the scene: what the sun sees.
    bool showShadowMap_ = false;
    // The shadows themselves, where the user sets them.
    ShadowSettings* shadowSettings_ = nullptr;
    // Console lives under the viewport and the inspector now, so it is always
    // there; these three are its level filters.
    bool consoleWarn_ = true;
    bool consoleError_ = true;
    bool consoleInfo_ = true;
    // Which top-bar menu is open, or -1. All five open something.
    int openMenu_ = -1;
    // The two panels that hang off the right-hand cluster rather than off a menu.
    bool layersOpen_ = false;
    int helpPage_ = 0;  // 0 none, 1 keyboard and mouse, 2 about
    // True on the frame a panel was opened, so that same press does not also close
    // it - and so that no state changes after the frame has been drawn, which
    // on-demand rendering would leave on screen until something else woke it.
    bool overlayOpenedThisFrame_ = false;
    float hierarchyScroll_ = 0.0f;
    // Inspector body: where it is scrolled to, and how tall its content turned
    // out to be last frame. The second number is measured, not predicted - the
    // sections add up as they are drawn - and it is what the scroll is clamped
    // against, so a body shorter than the dock cannot be scrolled into emptiness.
    float inspectorScroll_ = 0.0f;
    float inspectorContentHeight_ = 0.0f;
    TextField filter_;
    // Hierarchy filter. Its own field, because filtering the scene tree and
    // filtering the inspector are different questions.
    TextField hierarchyFilter_;
    // Command search in the top bar: type part of a menu row, Enter runs it.
    TextField toolbarSearch_;
    std::string projectName_;
    Camera homeCamera_;
    LayoutRects layout_;

    // --- gizmo drag ---------------------------------------------------------
    int dragAxis_ = -1;
    Vec3 dragOrigin_;
    float dragStartAxisT_ = 0.0f;
    float dragStartScale_ = 1.0f;
    // Rotate: the orientation the body had when the ring was grabbed, the axis it is
    // turning about, and where the pointer stood on the ring. The turn is then
    // built from screen-space steps, not from one absolute angle re-read every
    // frame - see Editor::ringScreenAngle() for what that buys.
    Quat dragStartOrientation_;
    Vec3 rotateAxis_;
    float rotateAccumulated_ = 0.0f;
    float rotateLastScreenAngle_ = 0.0f;
    bool rotateAngleValid_ = false;
    // Pan: the pointer and camera positions when the drag began.
    Vec3 panStartPointer_;
    Vec3 panStartCamera_;
    float panPixelsPerUnit_ = 0.0f;
    Tool tool_ = Tool::Move;

    ViewRay viewRay_;

    const char* text(const char* key) const { return loc_.get(key); }
};

} // namespace s3d