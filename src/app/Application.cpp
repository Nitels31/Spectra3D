#include "app/Application.hpp"

#include "core/CrashLog.hpp"
#include "core/Log.hpp"
#include "core/Str.hpp"
#include "platform/Filesystem.hpp"
#include "render/GlInfo.hpp"
#include "scene/SceneSerializer.hpp"

#include <GL/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

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

Application::Application() : launcher_(game_.localizer()) {}

Application::~Application() = default;

bool Application::initialize() {
    // The program folder first: the crash report is written next to the
    // program, and a crash before the window exists has to be on the record
    // too. Then the handlers, before anything else can go wrong.
    GameHost::adoptProgramFolder();
    CrashLog::setProgramName(kEngineName);
    CrashLog::install();

    Log::info("Spectra3D starting");
    std::printf("Spectra3D | OpenGL 3.3 compat | GLFW | FreeType\n");

    // The window, the renderer, the sky, the sound and the script host. The
    // game is the shell's: the editor plays it, and the player program is it
    // and nothing else.
    if (!game_.initialize(kEngineName)) {
        CrashLog::write("The window could not be opened");
        return false;
    }

    // The interface of the editor draws itself with the fonts the engine
    // ships, and the launcher draws itself with the same ones. If the fonts
    // cannot be baked, every line of text in the editor comes out blank - so
    // the failure is loud and immediate instead of a silent picture with no
    // letters in it.
    if (!uiRenderer_.buildFonts())
        Log::error("Editor: the font atlas could not be baked; every line of text "
                   "will come out blank");

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
    game_.window().applyIcon(launcher_.settings().appIcon.c_str());
    inEditor_ = false;

    reportStartupInfo();
    return true;
}

void Application::reportStartupInfo() {
    Log::info("Window %dx%d", game_.window().framebufferWidth(),
              game_.window().framebufferHeight());
    Log::info("OpenGL: requested %s compat, driver reports %s", gl::requestedVersion(),
              gl::driverVersionString());
    Log::info("GPU: %s", (const char*)glGetString(GL_RENDERER));
    Log::info("Text: %s", uiRenderer_.fontsReady() ? "FreeType OK" : "metrics fallback");
#if SPECTRA_SCRIPTING
    Log::info("Scripting: %s", game_.scripts().available() ? game_.scripts().engineVersion().c_str() : "off");
#else
    Log::info("Scripting: off");
#endif
    Log::info("Projects: %s", launcher_.projects().root().c_str());
}

int Application::run() {
    while (game_.window().pumpEvents()) {
        // The frame the loop is waiting for, and the seconds since the last
        // one. The wait is inside the game: the cap and the idle tick are the
        // same in the editor and in a game.
        float dt = 0.0f;
        const bool busy = game_.window().consumeActivity() || isSimulating();
        if (!game_.beginFrame(busy, dt)) continue;

        // Esc belongs to the launcher here. In the editor it is the panels' - the
        // open menu, the search field, the numeric value editor - and this call
        // would eat the edge before any of them got to read it.
        if (!inEditor_ && game_.window().takeKeyPress(GLFW_KEY_ESCAPE)) {
            game_.window().requestClose();
            continue;
        }

        if (inEditor_) {
            // The loader and the landform make things on as many threads as the
            // user asked for, and the setting is read every frame so a change
            // takes hold at once.
            editor_->setLoaderThreads(launcher_.settings().loaderThreads);
            runEditorFrame((float)dt);
        } else
            runLauncherFrame();

        // And the picture, before the buffer is swapped. It goes into the
        // system's temporary folder and not next to the program: a build
        // folder full of pictures is a build folder nobody can read.
        game_.takeScreenshotIfRequested();

        game_.window().present();

        // Everything this frame did not read dies with it - here, at the end of
        // the frame that had its chance to read it, and not at the top of the
        // loop: an event that arrives while the loop waits belongs to the next
        // frame, and dropping it before that frame is drawn loses the keystroke
        // that opened the project in the first place. Levels are not touched - a
        // key still held down keeps the window busy, which is the point - but an
        // edge nobody consumed does not stay latched forever.
        game_.window().dropUnreadInput();
    }
    return 0;
}

void Application::updateTitle() {
    // Icon, engine name, project name - exactly what the editor sketch has in the
    // title bar. There is no frame counter and no "idle" marker: the sketch has
    // neither, and a title that changes every second would also mean the one
    // second tick below has to rewrite it once a second even when nothing happens.
    char title[128];
    if (inEditor_ && !game_.project().name.empty())
        std::snprintf(title, sizeof(title), "%s - %s", kEngineName,
                      game_.project().name.c_str());
    else
        std::snprintf(title, sizeof(title), "%s", kEngineName);
    game_.window().setTitle(title);
}

void Application::runLauncherFrame() {
    std::string chosen;
    const Launcher::Result result =
        launcher_.update(uiRenderer_, launcherUi_, game_.window(),
                         game_.window().framebufferWidth(), game_.window().framebufferHeight(),
                         chosen);

    if (result == Launcher::Result::Quit) {
        game_.window().requestClose();
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
    // A project made before the layout changed is moved into the layout first:
    // the scene into `scenes/`, the models into `assets/models`. It happens once,
    // when the project is opened, and it is the only time the engine writes
    // into a project it did not create.
    ProjectInfo migrated = project;
    launcher_.projects().migrate(migrated);

    // How many threads the loader may use. The user picked the number, and the
    // models of a project are the slow part of opening it.
    game_.setLoaderThreads(launcher_.settings().loaderThreads);
    game_.loadProject(migrated);
    const std::string scenePath = ProjectLibrary::scenePath(game_.project());

    editor_ = std::make_unique<Editor>(game_.meshes(), game_.localizer(), game_.sound());
    // The editor changes the shadows where they can be seen, and they are the
    // project's: the editor is given the settings to change, and the
    // application puts them into the renderer and into the manifest.
    editor_->setShadowSettings(&game_.shadowSettings());
    // The canvas is the game's own: the editor edits the interface of the game
    // in place, and the scene file carries it into the build.
    editor_->setCanvas(&game_.canvas());
    // The fog is the renderer's own copy and the editor changes it where the
    // fog can be seen: the same arrangement as the shadows, one step shorter -
    // the fog has no map and no bias, so there is nothing to copy.
    editor_->setFogSettings(&game_.sceneRenderer().fogSettings());
    // The particles of the world: the editor adds emitters to them and takes
    // them away again, and the emitters are scene data - they go into the
    // scene file with everything else.
    editor_->setParticles(&game_.particles());
    editor_->setScenePath(scenePath);
    editor_->setProjectName(game_.project().name);
    // Captured after the manifest has been applied, so "reset camera" in the View
    // menu goes back to the view the project was saved with.
    editor_->setHomeCamera(game_.camera());
    inEditor_ = true;

    // The click that opened the project is not a click in the editor. Its press
    // was the launcher's, and its release arrives here - and a release with no
    // press in the editor is what made the first frame of the editor pick a
    // panel up and drag it. The edges die with the mode they belonged to.
    game_.window().dropUnreadInput();

    updateTitle();
    // The icon of the project is the icon of the window: a project that brings
    // a picture of its own is shown as itself.
    const std::string icon = ProjectLibrary::iconPath(game_.project());
    if (fs::exists(icon)) game_.window().applyIconFile(icon);
    Log::info("Project '%s' opened: %s", game_.project().name.c_str(), scenePath.c_str());
}

void Application::closeProject() {
    // onStop before the editor goes. The editor holds the snapshot the world is
    // restored from on Stop, and scripts are written against that world saying
    // goodbye to it - so a script left running while the world is cleared never
    // gets the callback, and whatever it spawned or changed is simply dropped
    // without being told. Only asked when playing: there is nothing to stop in
    // an editor that never started.
    if (editor_ && editor_->simulating()) {
#if SPECTRA_SCRIPTING
        game_.scripts().callStop();
#endif
#if SPECTRA_SPSCRIPT
        game_.spsScripts().callStop();
#endif
    }
    editor_.reset();
    game_.world().clear();
    inEditor_ = false;
    launcher_.applySettings();
    updateTitle();
}

void Application::runEditorFrame(float dt) {
    // Asked once, here, because consumeSingleStep() is CONSUMING: it clears the
    // request, so a second call later in the frame would answer false. Both the
    // per-frame hook and the fixed-step loop below need the same answer.
    const bool stepping = editor_->simulating() || editor_->consumeSingleStep();

    // The camera first: the game part of the frame. A click is answered with
    // the camera the user was looking through, and the editor's own input
    // comes after it and before the world is stepped.
    game_.updateCamera(dt, editor_->simulating());

    editor_->handleInput(game_.world(), game_.camera(), game_.window(),
                          game_.window().framebufferWidth(),
                          game_.window().framebufferHeight(), dt);

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
    case Editor::ShellRequest::SaveProject: {
        // The scene file the editor is using is part of the project. A project
        // whose scene is a JSON file has to say so in the manifest, or the next
        // time it is opened it goes back to the text file and the scene that
        // was saved is gone.
        //
        // The path is relative to the project folder, because that is what the
        // manifest holds: "scenes/scene.json", not the whole path on this
        // machine.
        game_.project().sceneFile = ProjectLibrary::relativeToProject(game_.project(), editor_->scenePath());
        // The shadows, as they are set now: they are the project's, and the
        // project is what is written out.
        const ShadowSettings& shadows = game_.shadowSettings();
        game_.project().shadowQuality = shadows.quality == ShadowQuality::Off     ? 0
                                        : shadows.quality == ShadowQuality::Filtered ? 2
                                        : shadows.quality == ShadowQuality::Soft     ? 3
                                                                                   : 1;
        game_.project().shadowResolution = shadows.resolution;
        game_.project().shadowCascades = shadows.cascades;
        game_.project().shadowSunAzimuth = shadows.sunAzimuth;
        game_.project().shadowSunElevation = shadows.sunElevation;
        game_.project().shadowBias = shadows.bias;
        game_.project().shadowNormalOffset = shadows.normalOffset;
        game_.project().shadowRadius = shadows.radius;
        // The fog, as it is set now: the renderer keeps it, because the
        // renderer is what draws it.
        const FogSettings& fog = game_.sceneRenderer().fogSettings();
        game_.project().fogDensity = fog.density;
        game_.project().fogFalloff = fog.falloff;
        game_.project().fogHeight = fog.height;
        game_.project().fogScatter = fog.scatter;
        game_.project().fogForward = fog.forward;
        if (!launcher_.projects().writeManifest(game_.project()))
            Log::error("Project: cannot write the manifest");
        else
            Log::info("Project '%s' saved", game_.project().name.c_str());
        break;
    }
    case Editor::ShellRequest::Build:
        stageBuild(editor_->buildFolder(), false);
        break;
    case Editor::ShellRequest::BuildRun:
        stageBuild(editor_->buildFolder(), true);
        break;
    case Editor::ShellRequest::ReloadScripts:
        // The scripts of the second language are compiled and run again,
        // without the game stopping: the compiler is inside their runtime.
        game_.reloadScripts();
        break;
    }

    if (editor_->consumeTimeReset()) game_.resetSimulationTime();
    // Stop BEFORE Start. A Stop and a Play in the same frame - the user hits Play
    // again on the same frame they hit Stop, or a script restarts itself - loads the
    // new scripts and then says goodbye to them, so onStop runs on a script that was
    // never told to start. The old scripts have to finish first.
    if (editor_->consumePlayStopped()) {
#if SPECTRA_SCRIPTING
        game_.scripts().callStop();
#endif
#if SPECTRA_SPSCRIPT
        game_.spsScripts().callStop();
#endif
    }
    if (editor_->consumePlayStarted()) game_.startScripts();

    // The world: the scripts and the physics.
    game_.updateWorld(dt, stepping);

    // The picture of the screen, in the editor and in the launcher alike.
    //
    // It is asked for here and taken at the end of the frame, before the buffer
    // is swapped.
    renderEditorScene();
}

// The scene is drawn in the viewport: the column between the two docks and
// above the console. It is the same rectangle the editor picks with, so the
// picture and the pointer are the same view of the world.
void Application::renderEditorScene() {
    const Rect rect = editor_->layout().viewport;
    const int fbHeight = game_.window().framebufferHeight();
    Viewport viewport;
    viewport.glX = (int)rect.x;
    viewport.glY = fbHeight - (int)rect.y - (int)rect.height;
    viewport.width = (int)rect.width;
    viewport.height = (int)rect.height;

    game_.renderWorld(viewport, editor_->showGrid(), editor_->showVectors(),
                       editor_->showShadowMap());
    editor_->renderOverlay(game_.sceneRenderer(), game_.world(), game_.sceneCamera());
    // The interface of the game, over the world.
    //
    // The buttons answer the pointer while the game is running. While it is
    // being laid out a click picks an element instead - the same canvas, drawn
    // by the same renderer, and the editor's handles around the element that
    // is being worked on.
    game_.renderGui(rect, editor_->simulating(), editor_->pointerX(), editor_->pointerY(),
                    editor_->pointerDown(), editor_->pointerPressed(),
                    editor_->pointerReleased());
    editor_->renderUi(uiRenderer_, game_.world(), game_.camera(), game_.window(),
                       game_.window().framebufferWidth(), game_.window().framebufferHeight());
}

// A build of a project that ships itself: the program, the runtime files it
// needs beside it, the assets it loads, and the project itself. The program is
// the player - the engine, a window and a loop - and not a copy of the editor:
// a build of a game is a program of its own.
//
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

    const std::string modelsOut = str::join(str::join(target, "models"), "models");
    int copied = 0;
    for (const std::string& name : names) {
        const std::string path = scenes::findMeshFile(name);
        if (path.empty()) continue;
        if (fs::copyTree(path, str::join(modelsOut, str::fileName(path)))) copied++;

        // The pictures the model refers to. They are the files the loader
        // recorded, so they are the files the game will look for.
        const int index = game_.meshes().find(name);
        if (index < 0) continue;
        for (const std::string& texture : game_.meshes().textureFilesOf(index)) {
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

    // A build is one folder: the program, the core it loads and the data of
    // the game beside it.
    //
    //   <build>/
    //     <Game>/
    //       <Game>.exe          the program
    //       SpectraPlayer.dll   the core of the engine
    //       <Game>_Data/
    //         boot.spectra      the manifest: where the game is
    //         scenes/           the scenes of the game
    //         scripts/          the scripts of the game
    //         assets/           the assets of the game
    //         plugins/          what the engine brings
    //
    // The program and the core are together because that is where Windows looks
    // for a library, and the data is in a folder of its own because that is where
    // the program looks for it: `<name>_Data` beside the program.
    const std::string gameName = game_.project().name.empty() ? "Game" : game_.project().name;
    const std::string gameDir = str::join(target, gameName);
    const std::string dataDir = str::join(gameDir, gameName + kBuiltGameDataSuffix);

    // The program itself, named after the game. It is the player: the engine, a
    // window and a loop, built next to the editor and copied here under the
    // name of the game. A build of a game is a program of its own, not the
    // editor with another name on it.
    //
    // copyTree handles a file as well as a folder, so the same call stages both
    // without a second copy routine.
    int copied = 0;
    const std::string player = findPlayerProgram();
    if (player.empty()) {
        Log::error("Build: the player program is not next to the editor ('%s')", exeDir.c_str());
        return false;
    }
    const std::string extension(str::extensionView(str::fileName(player)));
    const std::string gameExe = str::join(gameDir, gameName + extension);
    if (fs::copyTree(player, gameExe)) copied++;

    // The core of the engine, next to the program.
    const std::string core = findEngineCore();
    if (!core.empty() && fs::copyTree(core, str::join(gameDir, str::fileName(core)))) copied++;

    // Whatever else sits beside the program that is not a folder: the DLLs it
    // loads at startup. Copying the listing rather than a hardcoded list keeps a
    // new dependency from silently missing from the build. Only DLLs - a .txt
    // next to the binary belongs to the build system, not to a build.
    std::vector<fs::DirEntry> beside;
    fs::listDirectoryEx(exeDir, beside);
    for (const fs::DirEntry& entry : beside) {
        if (entry.isDir) continue;
        if (!str::equalsIgnoreAscii(str::extensionView(entry.name), ".dll")) continue;
        if (fs::copyTree(str::join(exeDir, entry.name), str::join(gameDir, entry.name))) copied++;
    }

    // The data of the game, entry by entry: the manifest, the scenes, the
    // scripts, the assets of the game and the icon.
    //
    // Entry by entry because the default build folder is inside the project, so
    // copying the project wholesale would copy the build into itself - and each
    // rebuild would double the size of the last one.
    if (!game_.project().path.empty()) {
        const std::string manifest = ProjectLibrary::manifestPath(game_.project().path);
        if (fs::exists(manifest))
            copied += fs::copyTree(manifest, str::join(dataDir, kProjectBootFile)) ? 1 : 0;

        const std::string icon = ProjectLibrary::iconPath(game_.project());
        if (fs::exists(icon))
            copied += fs::copyTree(icon, str::join(dataDir, kProjectIconFile)) ? 1 : 0;

        for (const char* folder : {kProjectScenesDir, kProjectScriptsDir, kProjectAssetsDir}) {
            const std::string from = str::join(game_.project().path, folder);
            if (!fs::isDirectory(from)) continue;
            if (fs::copyTree(from, str::join(dataDir, folder))) copied++;
        }
    }

    // What the engine brings: its languages, its font, its sounds, its sky and
    // its shaders. They are the plugins of the game - the parts that are the
    // engine's and not the game's.
    //
    // The models are the exception. A folder of them is the biggest thing in
    // the assets, and a build that carries all of them carries the ones nobody
    // will ever open - so only the ones the scene names are staged, with their
    // pictures. The project's own models travel with the project.
    const std::string assets = str::join(exeDir, "assets");
    const std::string plugins = str::join(dataDir, kBuiltGamePluginsDir);
    if (fs::isDirectory(assets)) {
        std::vector<fs::DirEntry> assetEntries;
        fs::listDirectoryEx(assets, assetEntries);
        for (const fs::DirEntry& entry : assetEntries) {
            if (entry.isDir && str::equalsIgnoreAscii(entry.name, "models")) continue;
            if (fs::copyTree(str::join(assets, entry.name), str::join(plugins, entry.name)))
                copied++;
        }
        copied += stageModels(assets, plugins, game_.project());
    }

    // The shaders, which are files the engine reads and not files it carries
    // inside itself. A build that starts and finds no shader is a build that
    // draws nothing.
    const std::string shaders = str::join(exeDir, "shaders");
    if (fs::isDirectory(shaders) && fs::copyTree(shaders, str::join(plugins, "shaders")))
        copied++;

    Log::info("Build: %d item(s) staged in '%s'", copied, str::join(target, gameName).c_str());

    if (!alsoRun) return true;

#if defined(_WIN32)
    const std::string run = gameExe;
    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    std::string command = "\"" + run + "\"";
    if (CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, gameDir.c_str(),
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
    const std::string run = gameExe;
    if (std::system(("chmod +x '" + run + "' && '" + run + "' &").c_str()) == 0) return true;
    Log::error("Build: cannot start '%s'", run.c_str());
    return false;
#endif
}

} // namespace s3d
