#pragma once
#include <memory>
#include <string>

#include "../assets/MeshRegistry.hpp"
#include "../core/Project.hpp"
#include "../editor/Editor.hpp"
#include "../i18n/Localizer.hpp"
#include "../physics/PhysicsWorld.hpp"
#include "../platform/Window.hpp"
#include "../render/Camera.hpp"
#include "../render/SceneRenderer.hpp"
#include "../render/UiRenderer.hpp"
#include "../script/ScriptHost.hpp"
#include "../ui/UiContext.hpp"
#include "FixedClock.hpp"
#include "Launcher.hpp"

namespace s3d {

// Composition root. Owns every long-lived subsystem and runs the frame loop;
// nothing below it reaches for global state.
//
// The application is a two-state machine: the launcher owns the window until a
// project is chosen, the editor owns it from then on.
class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Opens the window, initialises rendering and reports the startup banner.
    bool initialize();
    int run();

private:
    void runLauncherFrame();
    void runEditorFrame(float dt);
    void updateTitle();
    // Seconds between rendered frames, or 0 when uncapped.
    double frameInterval() const;
    // True while the editor's physics is stepping: the one case where the picture
    // changes without anyone touching anything.
    bool isSimulating() const { return inEditor_ && editor_ && editor_->simulating(); }

    // Hands the window over to the editor for `project`.
    void openProject(const ProjectInfo& project);
    void closeProject();

    void updateCameraInput(Window& window, float dt);
    void updateSimulation(float dt);
    void renderScene();
    void reportStartupInfo();

    // Runs every .as file in the project's scripts folder. Called when play mode
    // starts so scripts see a live world.
    void runProjectScripts();

    Window window_;
    PhysicsWorld world_;
    MeshRegistry meshes_;
    i18n::Localizer localizer_;
    Camera camera_;
    SceneRenderer sceneRenderer_;
    UiRenderer uiRenderer_;
    Launcher launcher_;
    UiContext launcherUi_;
    script::Host scripts_;
    std::unique_ptr<Editor> editor_;

    ProjectInfo project_;
    bool inEditor_ = false;

    // True while the window is untouched and nothing is simulating: the loop draws
    // one frame a second to keep the clock alive and otherwise leaves the GPU
    // alone.
    bool idle_ = false;

    FixedClock clock_{60.0, 10};
    double simulationTime_ = 0.0;
    double fpsAccumulator_ = 0.0;
    int fpsFrames_ = 0;
    double fps_ = 0.0;
    // Start of the current one-second window, on the window clock.
    double lastTickTime_ = 0.0;
    double previousTime_ = 0.0;
// When the next capped frame is due, on the window clock. Absolute, so the
    // period does not drift with the render time.
    double nextFrameTime_ = 0.0;
};

} // namespace s3d