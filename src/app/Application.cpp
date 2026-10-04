#include "Application.hpp"

#include "../core/Log.hpp"
#include "../core/Str.hpp"
#include "../platform/Filesystem.hpp"
#include "../render/GlInfo.hpp"
#include "../scene/SceneSerializer.hpp"
#include "../ui/Theme.hpp"

#include <GL/gl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace s3d {
namespace {

constexpr int kInitialWidth = 1280;
constexpr int kInitialHeight = 720;
constexpr double kMaxFrameTime = 0.1;

// How long an untouched window may sit without drawing anything. Long enough that
// the clock does not visibly stutter, short enough that a missed invalidation
// cannot hide for more than a moment.
constexpr double kIdleTickSeconds = 1.0;
constexpr float kCameraSpeed = 12.0f;
constexpr float kCameraBoost = 30.0f;
constexpr float kLookSensitivity = 0.0025f;

const GravityAxis kGravityAxes[6] = {GravityAxis::NegY, GravityAxis::PosY, GravityAxis::NegX,
                                     GravityAxis::PosX, GravityAxis::NegZ, GravityAxis::PosZ};

} // namespace

namespace {

// Waits until `deadline` seconds have passed on the window clock, still pumping
// events so the UI stays responsive.
//
// Sleep() cannot do this job on Windows: it is quantised to ~15.6 ms, so a 60 fps
// budget of 16.7 ms overshoots to two sleeps and the cap lands on 30. A
// high-resolution waitable timer has sub-millisecond precision. Older Windows
// falls back to a normal timer, and anything without either falls back to sliced
// sleeps.
//
// The wait lives here, in the frame loop, and not in Window: pacing a frame is the
// loop's decision, and the window knows nothing about frame budgets.
void waitForFrame(Window& window, double deadline) {
#if defined(_WIN32)
    static HANDLE timer = [] {
        HANDLE h = CreateWaitableTimerExW(nullptr, nullptr,
                                          CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                          TIMER_ALL_ACCESS);
        if (!h) h = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        return h;
    }();
#endif

    for (;;) {
        if (!window.pumpEvents()) return;
        const double left = deadline - window.time();
        if (left <= 0.0) return;

#if defined(_WIN32)
        if (timer) {
            LARGE_INTEGER due;  // 100 ns units, negative means "relative"
            due.QuadPart = -(LONGLONG)(left * 1.0e7);
            if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
                // Wait for whichever arrives first: the deadline, or a message.
                // Sleeping on the timer alone would make a click wait out the whole
                // frame, and POLLING the timer with a zero timeout instead of
                // sleeping burns a full core - both were measured, one at 95% of a
                // core for an idle window.
                MsgWaitForMultipleObjects(1, &timer, FALSE, (DWORD)(left * 1000.0) + 1,
                                          QS_ALLINPUT);
                // A waitable timer stays signalled after it fires, and this one is
                // manual reset, so it has to be disarmed or the next wait returns
                // instantly and the loop spins.
                CancelWaitableTimer(timer);
                continue;
            }
        }
        Sleep((DWORD)(std::fmin(left, 0.004) * 1000.0));
#else
        std::this_thread::sleep_for(std::chrono::duration<double>(std::fmin(left, 0.002)));
#endif
    }
}
} // namespace

Application::Application()
    : launcher_(localizer_), scripts_(world_, meshes_, camera_) {}

Application::~Application() = default;

bool Application::initialize() {
    // Anchor the process to the binary directory so logs and scenes land in
    // build/, then resolve the asset root relative to it.
    fs::adoptAssetWorkingDir();
    Log::setFileDirectory(fs::executableDir().empty() ? nullptr : fs::executableDir().c_str());

    Log::info("Spectra3D starting");
    std::printf("Spectra3D | OpenGL 3.3 compat | GLFW | FreeType\n");

    // The context does not exist yet, so the real version cannot go into the
    // title here; the per-second title update below fills it in once it can be
    // asked.
    if (!window_.create("Spectra3D", kInitialWidth, kInitialHeight)) {
        Log::error("Failed to create window");
        return false;
    }
    window_.setSizeLimits(kInitialWidth, kInitialHeight, 1920, 1080);

    localizer_.loadBundles();
    uiRenderer_.buildFonts();
    sceneRenderer_.initialize(world_.arenaMin, world_.arenaMax);

// The banner shows the context the engine actually holds, not the version the
    // driver likes to advertise: a 3.3 compatibility context on an NVIDIA card
    // answers "4.6.0 NVIDIA ..." to glGetString, and putting that in the menu bar
    // contradicts the title right next to it. The full driver string stays in the
    // log.
    launcher_.setGraphicsInfo((const char*)glGetString(GL_RENDERER), gl::requestedVersion());
    launcher_.initialize();
    inEditor_ = false;

    scripts_.setDiagnosticSink([](const script::Host::Diagnostic& d) {
        if (d.isError)
            Log::error("script %s (%d,%d): %s", d.section.c_str(), d.row, d.col,
                       d.message.c_str());
        else
            Log::info("script %s (%d,%d): %s", d.section.c_str(), d.row, d.col,
                      d.message.c_str());
    });
    scripts_.initialize();

    reportStartupInfo();
    previousTime_ = window_.time();
    return true;
}

void Application::reportStartupInfo() {
    Log::info("Window %dx%d", window_.framebufferWidth(), window_.framebufferHeight());
    Log::info("OpenGL: requested %s compat, driver reports %s", gl::requestedVersion(), gl::driverVersionString());
    Log::info("GPU: %s", (const char*)glGetString(GL_RENDERER));
    Log::info("Text: %s", uiRenderer_.fontsReady() ? "FreeType OK" : "metrics fallback");
    Log::info("Scripting: %s", scripts_.available() ? scripts_.engineVersion().c_str() : "off");
    Log::info("Projects: %s", launcher_.projects().root().c_str());
}

int Application::run() {
    while (window_.pumpEvents()) {
        double now = window_.time();

        // Nothing to draw, so draw nothing. A window nobody touches still ran 60
        // frames a second for as long as it was open, which on a laptop is heat
        // with no picture to show for it. Two things force a frame: the user did
        // something, or the physics is running. A third - once a second - keeps
        // the clock honest and lets a missed invalidation heal itself.
        const bool busy = window_.consumeActivity() || isSimulating();
        const double tickDue = lastTickTime_ + kIdleTickSeconds;
        if (!busy && now < tickDue) {
            idle_ = true;
            // Wait for the tick rather than spinning. The wait still pumps events,
            // so the next click is acted on at once, and previousTime_ moves with
            // it so resuming does not hand the physics a huge dt.
            waitForFrame(window_, tickDue);
            previousTime_ = window_.time();
            lastTickTime_ = previousTime_;
            updateTitle();
            continue;
        }
        idle_ = false;
        lastTickTime_ = now;

        // Frame cap as an ABSOLUTE schedule: the next frame is due at
        // nextFrameTime_, not "one budget after the previous one finished".
        // Adding the budget to a timestamp that already contains the render time
        // makes every period longer than asked for, and the cap lands quietly
        // below its own number - 60 came out as 42.
        const double budget = frameInterval();
        if (budget > 0.0) {
            if (nextFrameTime_ <= 0.0) nextFrameTime_ = now;
            if (now < nextFrameTime_) {
                waitForFrame(window_, nextFrameTime_);
                now = window_.time();
            }
            nextFrameTime_ += budget;
            // A stall - a modal dialog, a breakpoint, a dragged window - must not be
            // paid back as a burst of catch-up frames. Resynchronise instead.
            if (nextFrameTime_ < now) nextFrameTime_ = now + budget;
        }

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
            updateTitle();
        }

        if (window_.takeKeyPress(GLFW_KEY_ESCAPE) && !inEditor_) {
            window_.requestClose();
            continue;
        }

        if (inEditor_)
            runEditorFrame((float)dt);
        else
            runLauncherFrame();

        window_.present();
    }
    return 0;
}
double Application::frameInterval() const {
    const int cap = launcher_.settings().frameRateCap;
    return cap > 0 ? 1.0 / (double)cap : 0.0;
}

void Application::updateTitle() {
    char title[128];
    if (inEditor_)
        std::snprintf(title, sizeof(title), "%s | FPS %.0f", project_.name.c_str(), fps_);
    else if (idle_)
        // An idle window is not slow, it is not drawing at all; saying "FPS 1"
        // would be technically true and completely misleading.
        std::snprintf(title, sizeof(title), "Spectra3D | OpenGL %s | idle",
                      gl::requestedVersion());
    else
        std::snprintf(title, sizeof(title), "Spectra3D | OpenGL %s | FPS %.0f",
                      gl::requestedVersion(), fps_);
    window_.setTitle(title);
}

void Application::runLauncherFrame() {
    std::string chosen;
    const Launcher::Result result =
        launcher_.update(uiRenderer_, launcherUi_, window_, window_.framebufferWidth(),
                         window_.framebufferHeight(), chosen);

    if (result == Launcher::Result::Quit) {
        window_.requestClose();
        return;
    }
    if (result != Launcher::Result::OpenProject) return;

    ProjectInfo project;
    if (!launcher_.projects().readManifest(chosen, project)) {
        Log::error("Project: cannot open '%s'", chosen.c_str());
        return;
    }
    openProject(project);
}

void Application::openProject(const ProjectInfo& project) {
    project_ = project;
    world_.clear();
    world_.setGravity((GravityAxis)project_.gravity);
    camera_.position = project_.cameraPosition;
    camera_.yaw = project_.cameraYaw;
    camera_.pitch = project_.cameraPitch;
    camera_.fovDeg = project_.cameraFov;

    const std::string scenePath = ProjectLibrary::scenePath(project_);
    if (fs::exists(scenePath)) {
        // A missing or empty scene is a fresh project, not an error.
        scenes::load(world_, meshes_, scenePath);
    }

    editor_ = std::make_unique<Editor>(meshes_, localizer_);
    editor_->setScenePath(scenePath);
    inEditor_ = true;
    updateTitle();
    Log::info("Project '%s' opened: %s", project_.name.c_str(), scenePath.c_str());
    Log::info("Scene: %d bodies", (int)world_.bodies.size());
}

void Application::closeProject() {
    editor_.reset();
    world_.clear();
    inEditor_ = false;
    launcher_.applySettings();
    updateTitle();
}

void Application::runProjectScripts() {
    const std::string dir = ProjectLibrary::scriptsPath(project_);
    if (!fs::isDirectory(dir)) return;

    const std::vector<std::string> files = fs::listFilesByExtension(dir, {".as"});
    if (files.empty()) return;
    Log::info("Scripts: running %d file(s) from %s", (int)files.size(), dir.c_str());
    for (const std::string& file : files)
        if (!scripts_.runFile(file))
            Log::error("Scripts: %s failed to compile", str::fileName(file).c_str());
}

void Application::runEditorFrame(float dt) {
    updateCameraInput(window_, dt);

    for (int i = 0; i < 6; ++i)
        if (window_.takeKeyPress(GLFW_KEY_1 + i)) world_.setGravity(kGravityAxes[i]);

    editor_->handleInput(world_, camera_, window_, window_.framebufferWidth(),
                         window_.framebufferHeight());
    if (editor_->consumeTimeReset()) simulationTime_ = 0.0;
    if (editor_->consumePlayStarted()) runProjectScripts();

    updateSimulation(dt);
    renderScene();
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

    const float speed = window.keyDown(GLFW_KEY_LEFT_SHIFT) ||
                                window.keyDown(GLFW_KEY_RIGHT_SHIFT)
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

void Application::renderScene() {
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