#pragma once
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "assets/MeshRegistry.hpp"
#include "i18n/Localizer.hpp"
#include "physics/PhysicsWorld.hpp"
#include "render/Camera.hpp"
#include "render/SceneRenderer.hpp"
#include "render/UiRenderer.hpp"
#include "ui/UiContext.hpp"
#include "editor/AssetBrowser.hpp"
#include "editor/Gizmo.hpp"

namespace s3d {

// Panel geometry, recomputed once per frame from the framebuffer size. Panels
// read these instead of recomputing offsets.
//
// The shape follows the editor sketch: a top bar with the menus, the transport in
// its middle and the search/layers/help cluster on the right, the hierarchy down
// the left, the viewport in the middle with its own header strip and a tool
// column down its left edge, the inspector on the right, and a console across the
// bottom under the viewport and the inspector.
struct EditorLayout {
    Rect menuBar;
    Rect viewport;
    Rect sceneHeader;    // "Scene" strip across the top of the viewport
    Rect viewportTools;  // vertical tool column inside the viewport, on the left
};

// Panels that can be dragged between the edges and the middle of the window, the
// way Unity's are. Four of them, four places to be: two vertical edges, one
// bottom strip, and free float over the scene.
enum class PanelId { Hierarchy = 0, Inspector = 1, Console = 2, Favorites, PanelCount };
enum class PanelZone { Left, Right, Bottom, Float };

inline constexpr int kPanelCount = (int)PanelId::PanelCount;

// Godot-style editor shell. Owns selection, play mode and the UI context, and
// delegates drawing to the panels in `panels/`.
class Editor {
public:
    // Widget id ranges, kept disjoint so panels can never collide.
    enum : int {
        kTopBar = 100,
        kTransportPlay = 101,
        kTransportPause = 102,
        kTransportStop = 103,
        kHierarchyRow = 400,
        kHierarchyStar = 420,
        kHierarchyFavOnly = 421,
        kHierarchySearch = 440,
        kFavoriteRow = 460,
        kInspectorDock = 500,
        kInspectorProperty = 600,
        kFilterField = 700,
        kSceneHeader = 800,
        kConsoleFilter = 950,
        // Top bar, right-hand cluster. 1000+ so nothing else can collide with it.
        kTopBarTool = 990,  // + tool index, the five tools in the bar itself
        kToolbarHelp = 1000,
        kToolbarLayers = 1001,
        kToolbarReload = 1002,
        kToolbarSearch = 1003,
        kToolbarCounter = 1004,
        kMenuRow = 1100,   // + row index
        kLayerRow = 1200,  // + body index
        kLayerToggle = 1300,
        kHelpClose = 1400,
        kPanelGrip = 1500,
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
    // Shown in the window title, the way the sketch has it: engine, dash, project.
    void setProjectName(std::string name) { projectName_ = std::move(name); }
    // The camera the project was opened with. "Reset camera" goes back to it
    // rather than to a hardcoded default, so a project that was saved looking at
    // something gets that something back.
    void setHomeCamera(const Camera& camera) { homeCamera_ = camera; }

    const char* scenePath() const {
        return scenePath_.empty() ? "scene.txt" : scenePath_.c_str();
    }

    // Saved the scene since it was last loaded or written. New Scene, Open Scene
    // and New Project all ask about it first, because there is no autosave and
    // nothing else stands between a stray click and an afternoon of work.
    bool sceneDirty() const { return sceneDirty_; }
    // The application owns gravity (it sets it from the manifest), so a keyboard
    // axis change there has to be able to say "this is an edit".
    void markSceneDirty() { sceneDirty_ = true; }
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
    const EditorLayout& layout() const { return layout_; }

    void saveScene(const PhysicsWorld& world);
    void loadScene(PhysicsWorld& world);

private:
    // Bounds for every panel size, wherever the size came from: a drag, a layout
    // file, a hand-edited one. Shared rather than repeated, because "too small to
    // be a panel" has to mean the same number in all three places.
    static constexpr float kPanelMinWidth = 160.0f;
    static constexpr float kPanelMinHeight = 88.0f;
    static constexpr float kDockMinWidth = 180.0f;
    static constexpr float kDockMaxWidth = 520.0f;
    static constexpr float kConsoleMinHeight = 80.0f;

    // --- input phases -------------------------------------------------------
    void handleHotkeys(PhysicsWorld& world, Window& window);
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
    enum class Modal { None, NewScene, OpenScene, SaveAs, BuildSettings, SaveFirst };
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
    float leftWidth_ = 260.0f;
    float rightWidth_ = 280.0f;
    float bottomHeight_ = 156.0f;

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
        ClearSelection,
        Wake,
        Grid,
        Vectors,
        FrameSelection,
        ResetCamera,
        Shape,
        Model,
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
    // Points the camera at the selection, far enough back that it fits.
    void frameSelection(PhysicsWorld& world, Camera& camera);

    static constexpr int kMaxMenuRows = 72;
    MenuRow menuRows_[kMaxMenuRows];
    int menuFirst_[kMenuCount + 1] = {};
    // Where the search field and the layers button ended up this frame, so the
    // panels that hang off them can be anchored to exactly the same rectangles.
    Rect searchField_;
    Rect layersButton_;
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
    // One panel, wherever it currently is: its own rectangle decides the rest.
    void drawPanelById(UiRenderer& ui, PhysicsWorld& world, Window& window, PanelId id);
    void drawDockedPanels(UiRenderer& ui, PhysicsWorld& world, Window& window);
    void drawFloatingPanels(UiRenderer& ui, PhysicsWorld& world, Window& window);
    // Title bar of a panel: the handle it is dragged by, and the strip the drop
    // preview is anchored to.
    Rect panelHeader(PanelId id) const;
    Rect panelRect(PanelId id) const;
    Rect panelCloseRect(PanelId id) const;
    void drawPanelChrome(UiRenderer& ui, PanelId id);
    void handlePanelDrag(PhysicsWorld& world);
    void drawPanelDragFeedback(UiRenderer& ui);
    const char* panelTitle(PanelId id) const;
    PanelZone zoneAt(float x, float y) const;
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
    void drawMaterialSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y);
    void drawFlagsSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y);
    void drawAnchorSection(UiRenderer& ui, RigidBody& body, const Rect& dock, float& y);
    void drawStateSection(UiRenderer& ui, PhysicsWorld& world, RigidBody& body,
                          const Rect& dock, float& y);
    void drawNodeSection(UiRenderer& ui, const Rect& dock, float& y);

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
    bool showGrid_ = true;
    bool showVectors_ = true;
    // Snap while dragging: the position lands on a quarter of a unit instead of
    // wherever the ray happened to hit. The magnet in the viewport toolbar.
    bool snapEnabled_ = false;
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
    EditorLayout layout_;

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
