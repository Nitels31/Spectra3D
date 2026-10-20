#include "app/Application.hpp"

#include "core/Log.hpp"
#include "core/Str.hpp"
#include "platform/Filesystem.hpp"
#include "platform/Screenshot.hpp"
#include "render/GlInfo.hpp"
#include "scene/CameraEntity.hpp"
#include "scene/SceneSerializer.hpp"
#include "ui/Theme.hpp"

#include <GL/gl.h>

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

const GravityAxis kGravityAxes[6] = {GravityAxis::NegY, GravityAxis::PosY, GravityAxis::NegX,
                                      GravityAxis::PosX, GravityAxis::NegZ, GravityAxis::PosZ};

} // namespace


namespace {

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
// The wait lives here, in the frame loop, and not in Window: pacing a frame is the
// loop's decision, and the window knows nothing about frame budgets.
bool waitForFrame(Window& window, double deadline, bool wakeOnActivity) {
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
        if (!window.pumpEvents()) return false;
        // Peek, not consume: latching the cursor position here would make the loop
        // miss the very movement that woke this wait, and the highlight would then
        // have to wait out the whole idle tick - which is exactly the second of
        // lag it is supposed to end.
        if (wakeOnActivity && window.peekActivity()) return true;
        const double left = deadline - window.time();
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
} // namespace

Application::Application()
    : launcher_(localizer_), scripts_(world_, meshes_, camera_, &window_, &sound_) {}

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
    sceneRenderer_.initialize();
    loadSky();
    startAudio();

    // The banner shows the context the engine actually holds, not the version the
    // driver likes to advertise: a 3.3 compatibility context on an NVIDIA card
    // answers "4.6.0 NVIDIA ..." to glGetString, and putting that in the menu bar
    // contradicts the title right next to it. The full driver string stays in the
    // log.
    //
    // The icon goes on after the launcher, because the icon is a setting: the window
    // gets the file the user chose, not the one that happened to ship. Nothing has
    // been drawn yet, so this is still "before anything is on screen".
    launcher_.setGraphicsInfo((const char*)glGetString(GL_RENDERER), gl::requestedVersion());
    launcher_.initialize();
    window_.applyIcon(launcher_.settings().appIcon.c_str());
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

void Application::loadSky() {
    // The sky is a file, not something the engine makes: a picture of the sky in
    // one piece, or a folder of six faces.
    //
    // The project's own sky wins over the engine's, the way a project's scene
    // wins over the engine's default.
    //
    // The engine's is looked for here, and the project's when the project is
    // opened - that is when the project is known.
    const std::string assets = fs::assetRoot();
    if (!assets.empty()) loadSkyFrom(assets);
}

void Application::loadSkyFrom(const std::string& directory) {
    // A picture of the sky in one piece, twice as wide as it is tall.
    for (const char* extension : {".jpg", ".jpeg", ".png", ".bmp"}) {
        const std::string path = str::join(directory, std::string("sky") + extension);
        if (!fs::exists(path)) continue;
        if (sceneRenderer_.sky().loadImage(path)) return;
    }
    // Or a folder of six faces.
    sceneRenderer_.sky().load(str::join(directory, "sky"));
}

void Application::loadProjectSky(const std::string& projectDir) {
    if (projectDir.empty()) return;
    // A project that has a sky of its own uses it; one that has not keeps
    // whatever the engine is using.
    loadSkyFrom(projectDir);
}

void Application::startAudio() {
    // The device first: without one there is nothing to load the sounds into,
    // and loading them anyway would fill the log with complaints about a
    // machine that cannot make a sound in the first place.
    if (!sound_.start()) return;

    // The sounds the engine ships. The name of each is the name of its file
    // without the extension, so a file dropped into assets/sounds/ is a sound
    // the scripts can play and a body can carry.
    const std::string assets = fs::assetRoot();
    if (assets.empty()) return;
    const int loaded = sound_.loadFolder(str::join(assets, "sounds"));
    Log::info("Audio: %d sound(s) loaded", loaded);
}

void Application::playImpactSounds() {
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
        // something, or the physics is running. A third - once a second - heals
        // a missed invalidation.
        const bool busy = window_.consumeActivity() || isSimulating();
        const double tickDue = lastTickTime_ + kIdleTickSeconds;
        if (!busy && now < tickDue) {
            // Sleep until the tick, not until the next frame: the wait wakes the
            // instant something happens, so the cursor moving over a button is
            // highlighted on the very next frame instead of a second later, and
            // an untouched window still draws one frame per second and no more.
            // previousTime_ moves with it, so resuming does not hand the physics
            // a huge dt.
            waitForFrame(window_, tickDue, true);
            previousTime_ = window_.time();
            continue;
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
                waitForFrame(window_, nextFrameTime_, false);
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

// Esc belongs to the launcher here. In the editor it is the panels' - the
        // open menu, the search field, the numeric value editor - and this call
        // would eat the edge before any of them got to read it.
        if (!inEditor_ && window_.takeKeyPress(GLFW_KEY_ESCAPE)) {
            window_.requestClose();
            continue;
        }

        // The picture of the screen, in the editor and in the launcher alike.
        //
        // It is asked for here and taken at the end of the frame, because the
        // picture has to be read out of the framebuffer before it is swapped -
        // after the swap the picture on the screen is the one that was there
        // before. And it is asked for here, before the editor's own input, so
        // that a panel with the keyboard cannot take the key away.
        if (window_.takeKeyPress(GLFW_KEY_F12)) screenshotRequested_ = true;

if (inEditor_) {
            // The loader and the landform make things on as many threads as the
            // user asked for, and the setting is read every frame so a change
            // takes hold at once.
            editor_->setLoaderThreads(launcher_.settings().loaderThreads);
            runEditorFrame((float)dt);
        }
        else
            runLauncherFrame();

        // And the picture, before the buffer is swapped. It goes into the
        // system's temporary folder and not next to the program: a build
        // folder full of pictures is a build folder nobody can read.
        if (screenshotRequested_) {
            screenshotRequested_ = false;
            const std::string folder = screenshot::pictureFolder();
            const std::string path = screenshot::nextPath(folder);
            if (screenshot::save(path, window_.framebufferWidth(), window_.framebufferHeight()))
                Log::info("The picture is in '%s'", path.c_str());
        }

        window_.present();


        // Everything this frame did not read dies with it - here, at the end of
        // the frame that had its chance to read it, and not at the top of the
        // loop: an event that arrives while the loop waits belongs to the next
        // frame, and dropping it before that frame is drawn loses the keystroke
        // that opened the project in the first place. Levels are not touched - a
        // key still held down keeps the window busy, which is the point - but an
        // edge nobody consumed does not stay latched forever.
        window_.dropUnreadInput();
    }
    return 0;
}

double Application::frameInterval() const {
    const int cap = launcher_.settings().frameRateCap;
    return cap > 0 ? 1.0 / (double)cap : 0.0;
}

void Application::updateTitle() {
    // Icon, engine name, project name - exactly what the editor sketch has in the
    // title bar. There is no frame counter and no "idle" marker: the sketch has
    // neither, and a title that changes every second would also mean the one
    // second tick below has to rewrite it once a second even when nothing happens.
    char title[128];
    if (inEditor_ && !project_.name.empty())
        std::snprintf(title, sizeof(title), "%s - %s", kEngineName, project_.name.c_str());
    else
        std::snprintf(title, sizeof(title), "%s", kEngineName);
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

// The world a project opens with: the gravity, the camera, the models and the
// scene. A game needs this much and an editor needs this much.
void Application::loadProjectWorld(const ProjectInfo& project) {
    project_ = project;
    world_.clear();
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
    sceneRenderer_.setShadowSettings(shadowSettings_);

    const std::string scenePath = ProjectLibrary::scenePath(project_);
    // Before the scene is read: loading a model is what pulls its textures in, so
    // the mesh registry needs to know where to put them.
    meshes_.setTextureRegistry(&textures_);
    // How many threads the loader may use. The user picked the number, and the
    // models of a project are the slow part of opening it.
    meshes_.setLoaderThreads(launcher_.settings().loaderThreads);

    // A project may have a sky of its own, and it wins over the engine's.
    loadProjectSky(project_.path);
    if (fs::exists(scenePath)) {
        // A missing or empty scene is a fresh project, not an error.
        scenes::load(world_, meshes_, scenePath);
    }
}

void Application::openProject(const ProjectInfo& project) {
    loadProjectWorld(project);
    const std::string scenePath = ProjectLibrary::scenePath(project_);

    editor_ = std::make_unique<Editor>(meshes_, localizer_, sound_);
    // The editor changes the shadows where they can be seen, and they are the
    // project's: the editor is given the settings to change, and the
    // application puts them into the renderer and into the manifest.
    editor_->setShadowSettings(&shadowSettings_);
    editor_->setScenePath(scenePath);
    editor_->setProjectName(project_.name);
    // Captured after the manifest has been applied, so "reset camera" in the View
    // menu goes back to the view the project was saved with.
    editor_->setHomeCamera(camera_);
    inEditor_ = true;

    // The click that opened the project is not a click in the editor. Its press
    // was the launcher's, and its release arrives here - and a release with no
    // press in the editor is what made the first frame of the editor pick a
    // panel up and drag it. The edges die with the mode they belonged to.
    window_.dropUnreadInput();

    updateTitle();
    Log::info("Project '%s' opened: %s", project_.name.c_str(), scenePath.c_str());
    Log::info("Scene: %d bodies", (int)world_.bodies.size());
}

void Application::closeProject() {
    // onStop before the editor goes. The editor holds the snapshot the world is
    // restored from on Stop, and scripts are written against that world saying
    // goodbye to it - so a script left running while the world is cleared never
    // gets the callback, and whatever it spawned or changed is simply dropped
    // without being told. Only asked when playing: there is nothing to stop in an
    // editor that never started.
    if (editor_ && editor_->simulating()) scripts_.callStop();
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
    // Asked once, here, because consumeSingleStep() is CONSUMING: it clears the
    // request, so a second call later in the frame would answer false. Both the
    // per-frame hook and the fixed-step loop below need the same answer.
    const bool stepping = editor_->simulating() || editor_->consumeSingleStep();

    // The camera the game looks through: the scene's active one, or - when no
    // camera says it wants to be that - the first camera in the scene. A camera
    // that was just made and never told to be active is still a camera, and a
    // scene with a camera in it is a scene that means to be looked through.
    //
    // It is the camera of the GAME, not of the editor. The editor looks at the
    // scene with its own free camera, the way an editor does: the camera in the
    // scene is an object in the scene, and an object you cannot see and cannot
    // drag is an object you cannot place. Pressing Play is what hands the view
    // over to the camera - the same moment a game starts, the same moment a
    // game view appears.
    //
    // The index is taken, not the body: a body can be deleted by a script
    // between here and the frame's end, and an index is re-checked every time
    // it is used.
    int gameCamera = world_.findActiveCamera();
    if (gameCamera < 0) gameCamera = world_.findFirstCamera();
    const bool playing = editor_->simulating();
    // While the game is running and a camera is in charge, the camera entity
    // IS the view, and the free camera is pinned to it. The editor's fly
    // controls do not get to move the game's camera out from under the game:
    // what the game sees is where the entity is, whether a script moved it or
    // an anchor pulled it along behind a body.
    const bool gameCameraInCharge = playing && gameCamera >= 0;
    sceneCamera_ = gameCameraInCharge ? gameCamera : -1;

    // The free camera only moves when it is the one being looked through. With
    // a camera entity in charge the view belongs to the game, and the mouse and
    // the movement keys are not the game's to answer to.
    updateCameraInput(window_, dt, !gameCameraInCharge);

    // Gravity is part of what gets saved with the scene, so picking an axis is an
    // edit to the document, not a view setting.
    for (int i = 0; i < 6; ++i)
        if (window_.takeKeyPress(GLFW_KEY_1 + i)) {
            world_.setGravity(kGravityAxes[i]);
            editor_->markSceneDirty();
        }

    editor_->handleInput(world_, camera_, window_, window_.framebufferWidth(),
                         window_.framebufferHeight(), dt);


    // File > New Project / Open Project hand the window back to the launcher,
    // which is where a project list lives. Anything the editor cannot do by
    // itself - leaving itself, writing a manifest, starting a process - is asked
    // for here and answered by the shell around it.
    switch (editor_->consumeShellRequest()) {
    case Editor::ShellRequest::None:
        break;
    case Editor::ShellRequest::NewProject:
    case Editor::ShellRequest::OpenProject:
        closeProject();
        break;
    case Editor::ShellRequest::SaveProject:
        // The scene file the editor is using is part of the project. A project
        // whose scene is a JSON file has to say so in the manifest, or the next
        // time it is opened it goes back to the text file and the scene that
        // was saved is gone.
        project_.sceneFile = str::fileName(editor_->scenePath());
        // The shadows, as they are set now: they are the project's, and the
        // project is what is written out.
        project_.shadowQuality = shadowSettings_.quality == ShadowQuality::Off     ? 0
                                : shadowSettings_.quality == ShadowQuality::Filtered ? 2
                                : shadowSettings_.quality == ShadowQuality::Soft     ? 3
                                                                                   : 1;
        project_.shadowResolution = shadowSettings_.resolution;
        project_.shadowCascades = shadowSettings_.cascades;
        if (!launcher_.projects().writeManifest(project_))
            Log::error("Project: cannot write the manifest");
        else
            Log::info("Project '%s' saved", project_.name.c_str());
        break;
    case Editor::ShellRequest::Build:
        stageBuild(editor_->buildFolder(), false);
        break;
    case Editor::ShellRequest::BuildRun:
        stageBuild(editor_->buildFolder(), true);
        break;
    }

if (editor_->consumeTimeReset()) simulationTime_ = 0.0;
    // Stop BEFORE Start. A Stop and a Play in the same frame - the user hits Play
    // again on the same frame they hit Stop, or a script restarts itself - loads the
    // new scripts and then says goodbye to them, so onStop runs on a script that was
    // never told to start. The old scripts have to finish first.
    if (editor_->consumePlayStopped()) scripts_.callStop();
    if (editor_->consumePlayStarted()) runProjectScripts();

    // Once per RENDERED frame, and before the physics rather than inside it.
    //
    // The placement is the whole point. updateSimulation() below runs a loop of
    // zero to kMaxStepsPerRenderedFrame fixed steps, so a hook called from inside
    // that loop would fire up to six times in one frame on a slow machine and once
    // on a fast one - the same script moving a body at 30 FPS and at 144 Hz.
    // Visual work, camera smoothing and anything that wants the real frame time
    // belongs here, where one frame is one call. Simulation belongs in
    // onFixedUpdate, inside the loop, where the step is constant.
    //
// Skipped when not simulating, so an editor that is not playing costs one
    // boolean rather than a lookup - and a script that is not playing has no
    // business being told about frames. `stepping` covers the single-step button
    // too: the world advances one fixed step on it, and a script that only runs
    // while `simulating()` is true would watch the physics move without being
    // told, which is exactly the divergence single-step exists to debug.
    if (stepping) scripts_.callUpdate(dt);

    updateSimulation(dt, stepping);

    // The view is read out of the camera entity here, after the scripts and the
    // physics have had their say - so a camera a script moved this frame, or one
    // an anchor pulled along behind a body, is seen where it is now and not
    // where it was a frame ago.
    //
    // The free camera is pinned to it: whatever the editor's own controls did
    // to the free camera while the game was running, the game camera is where
    // the entity is.
    if (gameCameraInCharge) {
        if (const RigidBody* body = world_.find(sceneCamera_))
            camera_ = cameraFromEntity(*body);
    }

    renderScene();
}

// A build of a project that ships itself: the running program, the runtime files
// it needs beside it, the assets it loads, and the project itself. There is no
// separate player binary to look for - this file IS the player - so "build" is a
// staging folder that can be copied to another machine and run from there.
// The models the project uses, and their pictures, staged into the build.
//
// A model without its picture still draws, in its flat colour, and that is not
// what anybody meant - so the pictures go with it. The project's own models are
// already in the project folder and travel with it.
int Application::stageModels(const std::string& assets, const std::string& target,
                             const ProjectInfo& project) {
    const std::string scenePath = ProjectLibrary::scenePath(project);
    const std::vector<std::string> names = scenes::sceneMeshNames(scenePath);
    if (names.empty()) return 0;

    const std::string modelsOut = str::join(str::join(target, "assets"), "models");
    int copied = 0;
    for (const std::string& name : names) {
        const std::string path = scenes::findMeshFile(name);
        if (path.empty()) continue;
        if (fs::copyTree(path, str::join(modelsOut, str::fileName(path)))) copied++;

        // The pictures the model refers to. They are the files the loader
        // recorded, so they are the files the game will look for.
        const int index = meshes_.find(name);
        if (index < 0) continue;
        for (const std::string& texture : meshes_.textureFilesOf(index)) {
            if (texture.empty()) continue;
            if (fs::copyTree(texture, str::join(modelsOut, str::fileName(texture)))) copied++;
        }
    }
    Log::info("Build: %d model file(s) staged for %d model(s) named by the scene", copied,
              (int)names.size());
    return copied;
}

bool Application::stageBuild(const std::string& target, bool alsoRun) {
    if (target.empty()) {
        Log::error("Build: no folder chosen");
        return false;
    }
    const std::string exeDir = fs::executableDir();
    if (exeDir.empty()) {
        Log::error("Build: cannot locate the program folder");
        return false;
    }

    // The program itself. copyTree handles a file as well as a folder, so the
    // same call stages both without a second copy routine.
    int copied = 0;
    const std::string exe = fs::executablePath();
    if (!exe.empty() && fs::copyTree(exe, str::join(target, str::fileName(exe)))) copied++;

    // Whatever sits beside the program that is not a folder: the DLLs it loads at
    // startup. Copying the listing rather than a hardcoded list keeps a new
    // dependency from silently missing from the build. Only DLLs - a .txt next to
    // the binary belongs to the build system, not to a build.
    std::vector<fs::DirEntry> beside;
    fs::listDirectoryEx(exeDir, beside);
    for (const fs::DirEntry& entry : beside) {
        if (entry.isDir) continue;
        if (!str::equalsIgnoreAscii(str::extensionView(entry.name), ".dll")) continue;
        if (fs::copyTree(str::join(exeDir, entry.name), str::join(target, entry.name))) copied++;
    }

    // Assets travel with the build: a build that starts and then finds no models
    // and no language is a build that cannot be used.
    //
    // The models are the exception. A folder of them is the biggest thing in
    // the assets, and a build that carries all of them carries the ones nobody
    // will ever open - so only the ones the scene names are staged, with their
    // pictures. The project's own models travel with the project.
    const std::string assets = str::join(exeDir, "assets");
    if (fs::isDirectory(assets)) {
        std::vector<fs::DirEntry> assetEntries;
        fs::listDirectoryEx(assets, assetEntries);
        for (const fs::DirEntry& entry : assetEntries) {
            if (entry.isDir && str::equalsIgnoreAscii(entry.name, "models")) continue;
            if (fs::copyTree(str::join(assets, entry.name),
                             str::join(str::join(target, "assets"), entry.name)))
                copied++;
        }
        copied += stageModels(assets, target, project_);
    }

    // The project, entry by entry rather than as one tree, and into
    // "<build>/project/<name>". The folder name is not decoration: a built program
    // starts by asking the same "where are my projects" question the editor asks,
    // and the answer it expects is "project" next to the binary. Copy the project
    // anywhere else and the build runs to an empty launcher.
    //
    // Entry by entry because the default build folder is inside the project, so
    // copying it wholesale would copy the build into itself - and each rebuild
    // would double the size of the last one.
    if (!project_.path.empty()) {
        const std::string into = str::join(str::join(target, "project"), project_.name);
        const std::string buildName = str::fileName(target);
        std::vector<fs::DirEntry> entries;
        if (fs::listDirectoryEx(project_.path, entries)) {
            bool ok = true;
            for (const fs::DirEntry& entry : entries) {
                if (str::equalsIgnoreAscii(entry.name, buildName)) continue;
                ok = fs::copyTree(str::join(project_.path, entry.name),
                                  str::join(into, entry.name)) &&
                     ok;
            }
            if (ok) copied++;
        }
    }

    Log::info("Build: %d item(s) staged in '%s'", copied, target.c_str());

    if (!alsoRun) return true;

#if defined(_WIN32)
    const std::string run = str::join(target, str::fileName(exe));
    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    std::string command = "\"" + run + "\"";
    if (CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, target.c_str(),
                       &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        Log::info("Build: started '%s'", run.c_str());
        return true;
    }
    Log::error("Build: cannot start '%s' (%lu)", run.c_str(), GetLastError());
    return false;
#else
    // Elsewhere a shell does the same job; no window to steal focus from either.
    const std::string run = str::join(target, str::fileName(exe));
    if (std::system(("chmod +x '" + run + "' && '" + run + "' &").c_str()) == 0) return true;
    Log::error("Build: cannot start '%s'", run.c_str());
    return false;
#endif
}

void Application::updateCameraInput(Window& window, float dt, bool allowMove) {
    // The right button holds the mouse whether or not the camera is the one
    // that moves. A press in the viewport is not a click on whatever happens to
    // be under it, and that has to stay true while the game is running.
    window.setMouseCaptured(window.mouseDown(GLFW_MOUSE_BUTTON_RIGHT));

    // With a camera entity in charge, the free camera is pinned to it and the
    // mouse is not the game's to answer to.
    if (!allowMove) return;

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

void Application::updateSimulation(float dt, bool stepping) {
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

void Application::renderScene() {
    // The scene is drawn in the viewport: the column between the two docks and
    // above the console. It is the same rectangle the editor picks with, so the
    // picture and the pointer are the same view of the world.
    const Rect rect = editor_->layout().viewport;
    const int fbHeight = window_.framebufferHeight();
    Viewport viewport;
    viewport.glX = (int)rect.x;
    viewport.glY = fbHeight - (int)rect.y - (int)rect.height;
    viewport.width = (int)rect.width;
    viewport.height = (int)rect.height;

    sceneRenderer_.beginFrame(window_.framebufferWidth(), window_.framebufferHeight());
    // The shadows, as they are set right now. The editor changes them where
    // the shadows can be seen, and they are the project's.
    sceneRenderer_.setShadowSettings(shadowSettings_);
    sceneRenderer_.setShowShadowDebug(editor_->showShadowMap());
    sceneRenderer_.renderWorld(world_, meshes_, textures_, camera_, viewport,
                               editor_->showGrid(), editor_->showVectors(), clockAlpha_,
                               sceneCamera_);
    editor_->renderOverlay(sceneRenderer_, world_, sceneCamera_);
    editor_->renderUi(uiRenderer_, world_, camera_, window_, window_.framebufferWidth(),
                      window_.framebufferHeight());
}

} // namespace s3d