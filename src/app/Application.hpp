#pragma once
#include <memory>

#include "../assets/MeshRegistry.hpp"
#include "../editor/Editor.hpp"
#include "../i18n/Localizer.hpp"
#include "../physics/PhysicsWorld.hpp"
#include "../platform/Window.hpp"
#include "../render/Camera.hpp"
#include "../render/SceneRenderer.hpp"
#include "../render/UiRenderer.hpp"
#include "FixedClock.hpp"

namespace s3d {

// Composition root. Owns every long-lived subsystem and runs the frame loop;
// nothing below it reaches for global state.
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
    void updateCameraInput(Window& window, float dt);
    void updateSimulation(float dt);
    void render();
    void reportStartupInfo();

    Window window_;
    PhysicsWorld world_;
    MeshRegistry meshes_;
    i18n::Localizer localizer_;
    Camera camera_;
    SceneRenderer sceneRenderer_;
    UiRenderer uiRenderer_;
    std::unique_ptr<Editor> editor_;

    FixedClock clock_{60.0, 10};
    double simulationTime_ = 0.0;
    double fpsAccumulator_ = 0.0;
    int fpsFrames_ = 0;
    double fps_ = 0.0;
    double previousTime_ = 0.0;
};

} // namespace s3d