#include "app/GameHost.hpp"

#include "core/CrashLog.hpp"
#include "core/Log.hpp"
#include "core/Str.hpp"
#include "platform/Filesystem.hpp"
#include "platform/Screenshot.hpp"
#include "scene/CameraEntity.hpp"
#include "scene/SceneSerializer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
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

// The impulse at which a hit is as loud as it gets. Past this the sound is as
// loud as it is going to be: a body that falls from the sky should not deafen.
constexpr float kImpactFullScale = 6.0f;

} // namespace

GameHost::GameHost()
    : scripts_(world_, meshes_, camera_, &window_, &sound_, &sceneRenderer_.particles(),
               &canvas_) {}

GameHost::~GameHost() = default;

void GameHost::adoptProgramFolder() {
    // Anchor the process to the binary directory so logs and scenes land
    // next to the program, and resolve the asset root relative to it.
    fs::adoptAssetWorkingDir();
    // The log lives next to the program, in the machine's own code page: the
    // file is opened by the narrow call, and a folder the machine cannot spell
    // is a folder with no log in it.
    Log::setFileDirectory(fs::executableDir().empty()
                              ? nullptr
                              : fs::systemPath(fs::executableDir()).c_str());
    // The crash report goes next to the log: the two are read together, and a
    // report in a folder nobody knows about is a report nobody reads.
    CrashLog::setDirectory(fs::executableDir().empty()
                               ? nullptr
                               : fs::systemPath(fs::executableDir()).c_str());
}

bool GameHost::initialize(const std::string& title) {
    // Anchor the process to the binary directory so logs and scenes land
    // next to the program, and resolve the asset root relative to it.
    adoptProgramFolder();

    if (!window_.create(title.c_str(), kInitialWidth, kInitialHeight)) {
        Log::error("Failed to create window");
        return false;
    }
    window_.setSizeLimits(kInitialWidth, kInitialHeight, 1920, 1080);

    localizer_.loadBundles();
    sceneRenderer_.initialize();
    guiRenderer_.buildFonts();
    loadSky();
    startAudio();

    scripts_.setDiagnosticSink([](const script::Host::Diagnostic& d) {
        if (d.isError)
            Log::error("script %s (%d,%d): %s", d.section.c_str(), d.row, d.col,
                       d.message.c_str());
        else
            Log::info("script %s (%d,%d): %s", d.section.c_str(), d.row, d.col,
                      d.message.c_str());
    });
    scripts_.initialize();

    previousTime_ = window_.time();
    return true;
}

// The world a project opens with: the gravity, the camera, the models and the
// scene. A game needs this much and an editor needs this much.
void GameHost::loadProject(const ProjectInfo& project) {
    project_ = project;
    world_.clear();

    // The assets of the project: its own content, and the engine's for
    // everything it did not bring. Set before anything is read, because the
    // models, the sky and the sounds are looked for in these two roots.
    fs::setProjectAssetRoot(ProjectLibrary::assetsPath(project_));

    world_.setGravity((GravityAxis)project_.gravity);
    camera_.position = project_.cameraPosition;
    camera_.yaw = project_.cameraYaw;
    camera_.pitch = project_.cameraPitch;
    camera_.fovDeg = project_.cameraFov;

    // The shadows, as the project asks for them. They are the project's and
    // not the machine's, so the scene looks the same everywhere.
    shadowSettings_.quality = project_.shadowQuality == 0   ? ShadowQuality::Off
                             : project_.shadowQuality == 2 ? ShadowQuality::Filtered
                             : project_.shadowQuality == 3 ? ShadowQuality::Soft
                                                          : ShadowQuality::Hard;
    shadowSettings_.resolution = project_.shadowResolution;
    shadowSettings_.cascades = project_.shadowCascades;
    shadowSettings_.sunAzimuth = project_.shadowSunAzimuth;
    shadowSettings_.sunElevation = project_.shadowSunElevation;
    shadowSettings_.bias = project_.shadowBias;
    shadowSettings_.normalOffset = project_.shadowNormalOffset;
    shadowSettings_.radius = project_.shadowRadius;
    sceneRenderer_.setShadowSettings(shadowSettings_);

    // The fog, as the project asks for it. It is the project's and not the
    // machine's, so the scene looks the same everywhere - and the renderer
    // keeps it, because the renderer is what draws the fog.
    FogSettings& fog = sceneRenderer_.fogSettings();
    fog.density = project_.fogDensity;
    fog.falloff = project_.fogFalloff;
    fog.height = project_.fogHeight;
    fog.scatter = project_.fogScatter;
    fog.forward = project_.fogForward;

    const std::string scenePath = ProjectLibrary::scenePath(project_);
    // Before the scene is read: loading a model is what pulls its textures in, so
    // the mesh registry needs to know where to put them.
    meshes_.setTextureRegistry(&textures_);
    // How many threads the loader may use. The user picked the number, and the
    // models of a project are the slow part of opening it.
    meshes_.setLoaderThreads(loaderThreads_);

    // A project may have a sky of its own, and it wins over the engine's.
    // The project's sky is in its own `assets/sky`: a picture of the sky in one
    // piece, or the six faces of a cube.
    loadProjectSky(fs::projectAssetRoot());
    loadProjectSounds();
    if (fs::exists(scenePath)) {
        // A missing or empty scene is a fresh project, not an error.
        //
        // The emitters of the world are in the scene file as well: an effect
        // that is in the picture and not in the file is an effect that is
        // gone when the scene is opened again.
        scenes::load(world_, meshes_, canvas_, scenePath, 1, &particles());
    }
    Log::info("Scene: %d bodies, %d interface element(s)", (int)world_.bodies.size(),
              (int)canvas_.elements.size());
}

// Runs every .as file in the project's scripts folder. This is what "play"
// means: the scripts see a live world.
void GameHost::startScripts() {
    const std::string dir = ProjectLibrary::scriptsPath(project_);
    if (!fs::isDirectory(dir)) return;

    const std::vector<std::string> files = fs::listFilesByExtension(dir, {".as"});
    if (files.empty()) return;
    Log::info("Scripts: running %d file(s) from %s", (int)files.size(), dir.c_str());
    for (const std::string& file : files)
        if (!scripts_.runFile(file))
            Log::error("Scripts: %s failed to compile", str::fileName(file).c_str());
}

int GameHost::findGameCamera() const {
    // The camera the game looks through: the scene's active one, or - when no
    // camera says it wants to be that - the first camera in the scene. A camera
    // that was just made and never told to be active is still a camera, and a
    // scene with a camera in it is a scene that means to be looked through.
    //
    // The index is taken, not the body: a body can be deleted by a script
    // between here and the frame's end, and an index is re-checked every time
    // it is used.
    int gameCamera = world_.findActiveCamera();
    if (gameCamera < 0) gameCamera = world_.findFirstCamera();
    return gameCamera;
}

void GameHost::updateCamera(float dt, bool running) {
    const int gameCamera = findGameCamera();

    // While the game is running and a camera is in charge, the camera entity
    // IS the view, and the free camera is pinned to it. The editor's fly
    // controls do not get to move the game's camera out from under the game:
    // what the game sees is where the entity is, whether a script moved it or
    // an anchor pulled it along behind a body.
    gameCameraInCharge_ = running && gameCamera >= 0;
    sceneCamera_ = gameCameraInCharge_ ? gameCamera : -1;

    // The free camera only moves when it is the one being looked through. With
    // a camera entity in charge the view belongs to the game, and the mouse and
    // the movement keys are not the game's to answer to.
    updateCameraInput(dt, !gameCameraInCharge_);
}

void GameHost::updateWorld(float dt, bool simulate) {
    // Once per RENDERED frame, and before the physics rather than inside it.
    //
    // The placement is the whole point. updateSimulation() below runs a loop of
    // zero to kMaxStepsPerRenderedFrame fixed steps, so a hook called from inside
    // that loop would fire up to six times in one frame on a slow machine and once
    // on a fast one - the same script moving a body at 30 FPS and at 144 Hz.
    // Visual work, camera smoothing and anything that wants the real frame time
    // belongs here, where one frame is one call. Simulation belongs in
    // onFixedUpdate, inside the loop, where the step is constant.
    if (simulate) scripts_.callUpdate(dt);

    // The particles. They are not physics and they are not bound to the
    // play mode: a fire in the scene burns while the editor sits still, and
    // it burns at the same rate in the editor and in a built game - one step
    // per rendered frame, with the real time of the frame.
    particles().update(dt, &world_);

    updateSimulation(dt, simulate);

    // The view is read out of the camera entity here, after the scripts and the
    // physics have had their say - so a camera a script moved this frame, or one
    // an anchor pulled along behind a body, is seen where it is now and not
    // where it was a frame ago.
    if (gameCameraInCharge_) {
        const RigidBody* body = world_.find(sceneCamera_);
        if (body) {
            // The projection is the mode's, not the entity's: a scene that is
            // looked at in 2D is looked at without perspective, and the
            // camera entity does not know about modes.
            const bool orthographic = camera_.orthographic;
            const float orthoSize = camera_.orthoSize;
            camera_ = cameraFromEntity(*body);
            camera_.orthographic = orthographic;
            camera_.orthoSize = orthoSize;
        }
    }
}

void GameHost::renderWorld(const Viewport& viewport, bool showGrid, bool showVectors,
                            bool showShadowDebug) {
    sceneRenderer_.beginFrame(window_.framebufferWidth(), window_.framebufferHeight());
    // The shadows, as they are set right now. The editor changes them where
    // the shadows can be seen, and they are the project's.
    sceneRenderer_.setShadowSettings(shadowSettings_);
    sceneRenderer_.setShowShadowDebug(showShadowDebug);
    sceneRenderer_.renderWorld(world_, meshes_, textures_, camera_, viewport, showGrid,
                               showVectors, clockAlpha_, (float)window_.time(), sceneCamera_);
}

void GameHost::renderGui(const Rect& area, bool interactive, float pointerX, float pointerY,
                          bool pointerDown, bool pointerPressed, bool pointerReleased) {
    // Nothing to draw: the canvas is empty, and a frame that sets up a
    // projection and draws nothing is a frame that costs something for
    // nothing.
    if (canvas_.elements.empty()) return;
    if (area.width < 1.0f || area.height < 1.0f) return;

    guiRenderer_.begin(window_.framebufferWidth(), window_.framebufferHeight(), area);
    guiRenderer_.draw(canvas_, area, textures_, interactive, pointerX, pointerY, pointerDown,
                      pointerPressed, pointerReleased);
    guiRenderer_.end();
}

bool GameHost::beginFrame(bool busy, float& dt) {
    double now = window_.time();

    // Nothing to draw, so draw nothing. A window nobody touches still ran 60
    // frames a second for as long as it was open, which on a laptop is heat
    // with no picture to show for it. Two things force a frame: the user did
    // something, or the physics is running. A third - once a second - heals
    // a missed invalidation.
    const double tickDue = lastTickTime_ + kIdleTickSeconds;
    if (!busy && now < tickDue) {
        // Sleep until the tick, not until the next frame: the wait wakes the
        // instant something happens, so the cursor moving over a button is
        // highlighted on the very next frame instead of a second later, and
        // an untouched window still draws one frame per second and no more.
        // previousTime_ moves with it, so resuming does not hand the physics
        // a huge dt.
        waitForFrame(tickDue, true);
        previousTime_ = window_.time();
        return false;
    }
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
            // No wake on activity here: the frame is coming anyway, so waiting
            // out the cap keeps the frame rate at the number that was asked for
            // instead of letting the mouse raise it.
            waitForFrame(nextFrameTime_, false);
            now = window_.time();
        }
        nextFrameTime_ += budget;
        // A stall - a modal dialog, a breakpoint, a dragged window - must not be
        // paid back as a burst of catch-up frames. Resynchronise instead.
        if (nextFrameTime_ < now) nextFrameTime_ = now + budget;
    }

    dt = (float)(now - previousTime_);
    previousTime_ = now;
    if (dt > kMaxFrameTime) dt = kMaxFrameTime; // avoid huge catch-up steps

    // The picture of the screen. It is asked for here and taken at the end of
    // the frame, because the picture has to be read out of the framebuffer
    // before it is swapped - after the swap the picture on the screen is the
    // one that was there before. And it is asked for here, before the shell's
    // own input, so that a panel with the keyboard cannot take the key away.
    if (window_.takeKeyPress(GLFW_KEY_F12)) screenshotRequested_ = true;

    return true;
}

void GameHost::takeScreenshotIfRequested() {
    if (!screenshotRequested_) return;
    screenshotRequested_ = false;
    // The picture goes into the system's temporary folder and not next to the
    // program: a build folder full of pictures is a build folder nobody can
    // read.
    const std::string folder = screenshot::pictureFolder();
    const std::string path = screenshot::nextPath(folder);
    if (screenshot::save(path, window_.framebufferWidth(), window_.framebufferHeight()))
        Log::info("The picture is in '%s'", path.c_str());
}

double GameHost::frameInterval() const {
    return frameRateCap_ > 0 ? 1.0 / (double)frameRateCap_ : 0.0;
}

void GameHost::setLoaderThreads(int threads) {
    loaderThreads_ = threads < 1 ? 1 : (threads > 6 ? 6 : threads);
    meshes_.setLoaderThreads(loaderThreads_);
}

// The sky is a file, not something the engine makes: a picture of the sky in
// one piece, or a folder of six faces.
//
// The project's own sky wins over the engine's, the way a project's scene
// wins over the engine's default.
//
// The engine's is looked for here, and the project's when the project is
// opened - that is when the project is known.
void GameHost::loadSky() {
    const std::string assets = fs::assetRoot();
    if (!assets.empty()) loadSkyFrom(assets);
}

void GameHost::loadSkyFrom(const std::string& directory) {
    // A picture of the sky in one piece, twice as wide as it is tall.
    for (const char* extension : {".jpg", ".jpeg", ".png", ".bmp"}) {
        const std::string path = str::join(directory, std::string("sky") + extension);
        if (!fs::exists(path)) continue;
        if (sceneRenderer_.sky().loadImage(path)) return;
    }
    // Or a folder of six faces.
    sceneRenderer_.sky().load(str::join(directory, "sky"));
}

void GameHost::loadProjectSky(const std::string& projectAssets) {
    if (projectAssets.empty()) return;
    // A project that has a sky of its own uses it; one that has not keeps
    // whatever the engine is using.
    loadSkyFrom(projectAssets);
}

void GameHost::startAudio() {
    // The device first: without one there is nothing to load the sounds into,
    // and loading them anyway would fill the log with complaints about a
    // machine that cannot make a sound in the first place.
    if (!sound_.start()) return;

    // The sounds the engine ships. The name of each is the name of its file
    // without the extension, so a file dropped into assets/sounds/ is a sound
    // the scripts can play and a body can carry.
    const std::string assets = fs::assetRoot();
    int loaded = 0;
    if (!assets.empty()) loaded += sound_.loadFolder(str::join(assets, "sounds"));
    Log::info("Audio: %d sound(s) loaded", loaded);
}

// The sounds of the project. They are loaded when the project is opened, after
// the engine's, and they replace the ones of the same name: a project that
// brings a sound of its own is heard, and the engine's is still there for the
// rest.
void GameHost::loadProjectSounds() {
    const std::string& projectAssets = fs::projectAssetRoot();
    if (projectAssets.empty()) return;
    int loaded = 0;
    loaded += sound_.loadFolder(str::join(projectAssets, assetdirs::kAudio[0]));
    loaded += sound_.loadFolder(str::join(projectAssets, assetdirs::kMusic[0]));
    if (loaded > 0) Log::info("Audio: %d sound(s) from the project", loaded);
}

void GameHost::playImpactSounds() {
    if (!sound_.running()) return;

    // The impulse each body took, summed over its contact points: a box on
    // the floor has four of them and each carries a quarter of its weight.
    // Summing first and playing once is the difference between one knock and
    // four.
    impactImpulses_.assign(world_.bodies.size(), 0.0f);
    for (const PhysicsWorld::Contact& contact : world_.contacts()) {
        const float impulse = contact.accumulatedNormal;
        if (impulse <= 0.0f) continue;
        if (contact.bodyA < impactImpulses_.size())
            impactImpulses_[contact.bodyA] += impulse;
        if (contact.bodyB != PhysicsWorld::Contact::kNoBody &&
            contact.bodyB < impactImpulses_.size())
            impactImpulses_[contact.bodyB] += impulse;
    }

    for (size_t i = 0; i < impactImpulses_.size(); ++i) {
        const float impulse = impactImpulses_[i];
        if (impulse <= 0.0f) continue;
        const RigidBody& body = world_.bodies[i];
        if (body.sound.empty()) continue;

        // What counts as a hit: a body resting on the floor carries its own
        // weight every step, and an impact carries its momentum. The line
        // between them is three times the weight - below it the body is
        // sitting there, above it something happened.
        const float resting = body.mass * world_.gravityMagnitude * (1.0f / 60.0f);
        if (impulse < resting * 3.0f) continue;

        // The loudness of a hit is how much of it there was, up to a point:
        // a body that falls from the sky should not deafen.
        const float loudness = clampf(impulse / kImpactFullScale, 0.2f, 1.0f);
        sound_.play(body.sound, body.soundVolume * loudness);
    }
}

void GameHost::updateCameraInput(float dt, bool allowMove) {
    // The right button holds the mouse whether or not the camera is the one
    // that moves. A press in the viewport is not a click on whatever happens to
    // be under it, and that has to stay true while the game is running.
    window_.setMouseCaptured(window_.mouseDown(GLFW_MOUSE_BUTTON_RIGHT));

    // With a camera entity in charge, the free camera is pinned to it and the
    // mouse is not the game's to answer to.
    if (!allowMove) return;

    if (window_.mouseCaptured() && !camera_.orthographic) {
        double dx = 0.0, dy = 0.0;
        window_.mouseDelta(dx, dy);
        camera_.rotate((float)(dx * kLookSensitivity), (float)(dy * kLookSensitivity));
    }

    Vec3 forward, right, up;
    camera_.basis(forward, right, up);

    // A flat world is looked at from the front and does not turn: the eye moves
    // in the plane of the picture and not into it, and the mouse does not
    // turn it.
    const Vec3 ahead = camera_.orthographic ? up : forward;

    Vec3 move;
    if (window_.keyDown(GLFW_KEY_W)) move += ahead;
    if (window_.keyDown(GLFW_KEY_S)) move -= ahead;
    if (window_.keyDown(GLFW_KEY_D)) move += right;
    if (window_.keyDown(GLFW_KEY_A)) move -= right;
    if (window_.keyDown(GLFW_KEY_SPACE)) move.y += 1.0f;
    if (window_.keyDown(GLFW_KEY_C)) move.y -= 1.0f;
    if (move.lengthSq() <= 0.0f) return;

    const float speed = window_.keyDown(GLFW_KEY_LEFT_SHIFT) ||
                                window_.keyDown(GLFW_KEY_RIGHT_SHIFT)
                            ? kCameraBoost
                            : kCameraSpeed;
    camera_.translate(move, speed, dt);
}

void GameHost::updateSimulation(float dt, bool stepping) {
    if (!stepping) {
        clock_.reset();
        // 1, not 0. The renderer draws lerp(prevTransform, currentTransform, alpha),
        // and alpha 0 would mean "draw the state before the last step" - which, in an
        // editor that is not stepping, is a step that never happened. Every body
        // would be drawn one step behind, and the hitbox and gizmo, which read
        // `position` directly, would not be drawn there at all.
        clockAlpha_ = 1.0f;
        return;
    }

    const int steps = clock_.advance((double)dt);
    for (int i = 0; i < steps; ++i) {
        // One call per fixed step, so a script sees the same dt on every machine
        // and the same number of calls for the same elapsed time. Zero to
        // kMaxStepsPerRenderedFrame of them in one frame.
        scripts_.callFixedUpdate((float)clock_.step());
        world_.step((float)clock_.step());
        simulationTime_ += clock_.step();
    }
    // The sounds of the hits, after the last step: the contacts are that
    // step's, and a sound that belongs to a step nobody can see any more is a
    // sound in the wrong place.
    playImpactSounds();
    // Read after the steps: this is how far the accumulator has spilled into the
    // next step, which is where the body visually is between the last two.
    clockAlpha_ = (float)clock_.alpha();
}

// Waits until `deadline` seconds have passed on the window clock, still pumping
// events so the UI stays responsive. With `wakeOnActivity` it returns as soon as
// the window reports activity instead, which is what makes a hover highlight
// appear on the frame the pointer moves.
//
// Sleep() cannot do this job on Windows: it is quantised to ~15.6 ms, so a 60 fps
// budget of 16.7 ms overshoots to two sleeps and the cap lands on 30. A
// high-resolution waitable timer has sub-millisecond precision. Older Windows
// falls back to a normal timer, and anything without either falls back to sliced
// sleeps.
//
// The wait lives here, in the frame loop, and not in Window: pacing a frame is
// the loop's decision, and the window knows nothing about frame budgets.
bool GameHost::waitForFrame(double deadline, bool wakeOnActivity) {
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
        if (!window_.pumpEvents()) return false;
        // Peek, not consume: latching the cursor position here would make the loop
        // miss the very movement that woke this wait, and the highlight would then
        // have to wait out the whole idle tick - which is exactly the second of
        // lag it is supposed to end.
        if (wakeOnActivity && window_.peekActivity()) return true;
        const double left = deadline - window_.time();
        if (left <= 0.0) return false;

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

} // namespace s3d
