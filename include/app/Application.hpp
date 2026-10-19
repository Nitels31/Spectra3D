#pragma once
#include <memory>
#include <string>

#include "assets/MeshRegistry.hpp"
#include "assets/TextureRegistry.hpp"
#include "core/Project.hpp"
#include "editor/Editor.hpp"
#include "i18n/Localizer.hpp"
#include "physics/PhysicsWorld.hpp"
#include "platform/Window.hpp"
#include "render/Camera.hpp"
#include "render/SceneRenderer.hpp"
#include "render/UiRenderer.hpp"
#include "script/ScriptHost.hpp"
#include "ui/UiContext.hpp"
#include "app/FixedClock.hpp"
#include "app/Launcher.hpp"

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
    void updateSimulation(float dt, bool stepping);
    void renderScene();
    void reportStartupInfo();

    // Runs every .as file in the project's scripts folder. Called when play mode
    // starts so scripts see a live world.
    void runProjectScripts();

    // --- what File > Build does ---------------------------------------------
    // The editor decides where the build goes; copying the program, its runtime
    // files and the project into that folder, and starting the copy, is the
    // shell's job. Returns false with the reason in the log when it cannot.
    bool stageBuild(const std::string& target, bool alsoRun);

    Window window_;
    PhysicsWorld world_;
    MeshRegistry meshes_;
    // Owned here rather than by the renderer: a texture outlives any one frame
    // and is shared by every body that uses the same image, so it belongs next to
    // the model registry and not inside the thing that draws.
    TextureRegistry textures_;
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

    // 60 Hz simulation, at most TWO steps per rendered frame.
    //
    // Two is FixedClock's own documented number, and it is not an arbitrary
    // safety limit: it is what keeps frame time proportional to the cost of a
    // step. `dt` is clamped to kMaxFrameTime, so a fully clamped frame can fund
    // six steps, and paying for six is what pins the frame rate to a plateau a
    // slower step cannot leave - because clearing the accumulator throws the debt
    // away instead of paying it down. See FixedClock.hpp.
    //
    // This used to say 10, which never bound: six is all a clamped frame can ask
    // for, so the value was a second number that had to agree with kMaxFrameTime
    // without agreeing with it.
    FixedClock clock_{60.0, 2};
    // How far the physics accumulator is into its next step, read by renderScene
    // so bodies are drawn interpolated between fixed steps rather than at them.
    // 1 until the first step says otherwise, because 1 is "draw the transform the
    // world actually has" and that is the right answer whenever nothing has moved.
    float clockAlpha_ = 1.0f;
    // The cap above, named because Application.cpp quotes it in a comment and a
    // hook budget is the thing that has to be reasoned about.
    static constexpr int kMaxStepsPerRenderedFrame = 2;
    double simulationTime_ = 0.0;
    // Start of the current one-second window, on the window clock.
    double lastTickTime_ = 0.0;
    double previousTime_ = 0.0;
// When the next capped frame is due, on the window clock. Absolute, so the
    // period does not drift with the render time.
    double nextFrameTime_ = 0.0;
};

} // namespace s3d