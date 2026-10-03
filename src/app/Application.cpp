#include "Application.hpp"

#include "../core/Log.hpp"
#include "../platform/Filesystem.hpp"
#include "../scene/BodyFactory.hpp"
#include "../ui/Theme.hpp"

#include <GL/gl.h>

#include <cstdio>

namespace s3d {
namespace {

constexpr int kInitialWidth = 1280;
constexpr int kInitialHeight = 720;
constexpr double kMaxFrameTime = 0.1;
constexpr float kCameraSpeed = 12.0f;
constexpr float kCameraBoost = 30.0f;
constexpr float kLookSensitivity = 0.0025f;

const GravityAxis kGravityAxes[6] = {GravityAxis::NegY, GravityAxis::PosY, GravityAxis::NegX,
                                     GravityAxis::PosX, GravityAxis::NegZ, GravityAxis::PosZ};

} // namespace

Application::Application() = default;

Application::~Application() = default;

bool Application::initialize() {
    // Anchor the process to the binary directory so logs and scenes land in
    // build/, then resolve the asset root relative to it.
    fs::adoptAssetWorkingDir();
    Log::setFileDirectory(fs::executableDir().empty() ? nullptr : fs::executableDir().c_str());

    Log::info("Spectra3D starting");
    std::printf("Spectra3D | OpenGL 2.1 | GLFW | FreeType\n");

    if (!window_.create("Spectra3D - OpenGL 2.1", kInitialWidth, kInitialHeight)) {
        Log::error("Failed to create window");
        return false;
    }
    window_.setSizeLimits(kInitialWidth, kInitialHeight, 1920, 1080);

    localizer_.loadBundles();
    uiRenderer_.buildFonts();

    world_.clear();
    sceneRenderer_.initialize(world_.arenaMin, world_.arenaMax);
    editor_ = std::make_unique<Editor>(meshes_, localizer_);

    reportStartupInfo();
    previousTime_ = window_.time();
    return true;
}

void Application::reportStartupInfo() {
    Log::info("Window %dx%d", window_.framebufferWidth(), window_.framebufferHeight());
    Log::info("OpenGL: %s", (const char*)glGetString(GL_VERSION));
    Log::info("GPU: %s", (const char*)glGetString(GL_RENDERER));
    Log::info("Text: %s", uiRenderer_.fontsReady() ? "FreeType OK" : "metrics fallback");
    Log::info("Scene: %d bodies", (int)world_.bodies.size());
}

int Application::run() {
    while (window_.pumpEvents()) {
        const double now = window_.time();
        double dt = now - previousTime_;
        previousTime_ = now;
        if (dt > kMaxFrameTime) dt = kMaxFrameTime; // avoid huge catch-up steps

        // FPS readout, refreshed once a second.
        fpsAccumulator_ += dt;
        ++fpsFrames_;
        if (fpsAccumulator_ >= 1.0) {
            fps_ = (double)fpsFrames_ / fpsAccumulator_;
            fpsFrames_ = 0;
            fpsAccumulator_ = 0.0;
            char title[128];
            std::snprintf(title, sizeof(title), "Spectra3D | OpenGL 2.1 | FPS %.0f", fps_);
            window_.setTitle(title);
        }

        updateCameraInput(window_, (float)dt);

        if (window_.takeKeyPress(GLFW_KEY_ESCAPE)) window_.requestClose();
        for (int i = 0; i < 6; ++i)
            if (window_.takeKeyPress(GLFW_KEY_1 + i)) world_.setGravity(kGravityAxes[i]);

        editor_->handleInput(world_, camera_, window_, window_.framebufferWidth(),
                             window_.framebufferHeight());
        if (editor_->consumeTimeReset()) simulationTime_ = 0.0;

        updateSimulation((float)dt);
        render();

        window_.present();
    }
    return 0;
}

void Application::updateCameraInput(Window& window, float dt) {
    window.setMouseCaptured(window.mouseDown(GLFW_MOUSE_BUTTON_RIGHT));

    if (window.mouseCaptured()) {
        double dx = 0.0, dy = 0.0;
        window.mouseDelta(dx, dy);
        camera_.rotate((float)(dx * kLookSensitivity), (float)(dy * kLookSensitivity));
    }

    Vec3 forward, right, up;
    camera_.basis(forward, right, up);

    Vec3 move;
    if (window.keyDown(GLFW_KEY_W)) move += forward;
    if (window.keyDown(GLFW_KEY_S)) move -= forward;
    if (window.keyDown(GLFW_KEY_D)) move += right;
    if (window.keyDown(GLFW_KEY_A)) move -= right;
    if (window.keyDown(GLFW_KEY_SPACE)) move.y += 1.0f;
    if (window.keyDown(GLFW_KEY_C)) move.y -= 1.0f;
    if (move.lengthSq() <= 0.0f) return;

    const float speed =
        window.keyDown(GLFW_KEY_LEFT_SHIFT) || window.keyDown(GLFW_KEY_RIGHT_SHIFT)
            ? kCameraBoost
            : kCameraSpeed;
    camera_.translate(move, speed, dt);
}

void Application::updateSimulation(float dt) {
    if (!editor_->simulating() && !editor_->consumeSingleStep()) {
        clock_.reset();
        return;
    }

    const int steps = clock_.advance((double)dt);
    for (int i = 0; i < steps; ++i) {
        world_.step((float)clock_.step());
        simulationTime_ += clock_.step();
    }
}

void Application::render() {
    const Rect viewport = editor_->layout().viewport;
    Viewport glViewport;
    glViewport.glX = (int)viewport.x;
    glViewport.glY = window_.framebufferHeight() - (int)viewport.y - (int)viewport.height;
    glViewport.width = (int)viewport.width;
    glViewport.height = (int)viewport.height;

    sceneRenderer_.beginFrame(window_.framebufferWidth(), window_.framebufferHeight());
    sceneRenderer_.renderWorld(world_, meshes_, camera_, glViewport, editor_->showGrid(),
                               editor_->showVectors());
    editor_->renderOverlay(sceneRenderer_, world_);
    editor_->renderUi(uiRenderer_, world_, camera_, window_, window_.framebufferWidth(),
                      window_.framebufferHeight());
}

} // namespace s3d