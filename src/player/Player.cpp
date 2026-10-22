#include "player/Player.hpp"

#include "core/CrashLog.hpp"
#include "core/Log.hpp"
#include "core/Str.hpp"
#include "platform/Filesystem.hpp"

namespace s3d {

bool Player::initialize() {
    // The log and the working directory first: a program that says it has no
    // game to play says it in the log next to the program, and not only on
    // the screen. Then the crash handlers, before anything else can go wrong.
    GameHost::adoptProgramFolder();
    CrashLog::setProgramName("SpectraPlayer");
    CrashLog::install();

    // The data of a built game is in the folder named after the program:
    // "MyGame.exe" plays "MyGame_Data". The boot file in it says which game
    // this is - the scene, the scripts and the assets - so the program does
    // not have to be told.
    const std::string data = builtGameDataDir();
    const std::string boot = data.empty() ? std::string() : str::join(data, kProjectBootFile);
    if (boot.empty() || !fs::exists(boot)) {
        Log::error("Player: no boot file beside the program");
        std::printf("Player: no boot file beside the program\n");
        CrashLog::write("There is no boot file beside the program");
        return false;
    }

    // The manifest of the game, read the way a project's is read: the same
    // keys, and the same reader, because a built game is a project that
    // ships itself.
    ProjectInfo info;
    if (!ProjectLibrary().readManifest(data, info)) {
        Log::error("Player: cannot read '%s'", boot.c_str());
        std::printf("Player: cannot read '%s'\n", boot.c_str());
        CrashLog::write("The boot file of the game could not be read");
        return false;
    }

    // The name of the game is in the report: a report of "SpectraPlayer" says
    // less than a report of the game that died.
    CrashLog::setProgramName(info.name.c_str());

    Log::info("Player: the game is '%s', in '%s'", info.name.c_str(), data.c_str());
    std::printf("%s | OpenGL 3.3 compat | GLFW | FreeType\n", info.name.c_str());

    // The window, the renderer, the sky, the sound and the script host. The
    // window says the name of the game.
    if (!game_.initialize(info.name)) return false;

    // The picture of the game is the picture of the window: the icon in the
    // data folder is the game's own.
    const std::string icon = str::join(data, kProjectIconFile);
    if (fs::exists(icon)) game_.window().applyIconFile(icon);

    // The world: the gravity, the camera, the models, the scene and the
    // sounds of the game.
    game_.loadProject(info);

    // The game starts: the scripts run, and the world steps from the first
    // frame. There is no Play button here - the program IS the play mode.
    game_.startScripts();

    Log::info("Player: '%s' is running", info.name.c_str());
    return true;
}

int Player::run() {
    while (game_.window().pumpEvents()) {
        // The frame the loop is waiting for, and the seconds since the last
        // one. A game is never idle: the world steps, so there is always a
        // frame to draw.
        float dt = 0.0f;
        if (!game_.beginFrame(true, dt)) continue;

        // Esc leaves the game. There is no launcher to go back to, so it
        // closes the window.
        if (game_.window().takeKeyPress(GLFW_KEY_ESCAPE)) {
            game_.window().requestClose();
            continue;
        }

        // The camera of the game, the scripts and the physics.
        game_.updateCamera(dt, true);
        game_.updateWorld(dt, true);

        // The picture: the whole window is the view of the game. No grid, no
        // vectors, no shadow map on the screen - those are the editor's.
        Viewport viewport;
        viewport.glX = 0;
        viewport.glY = 0;
        viewport.width = game_.window().framebufferWidth();
        viewport.height = game_.window().framebufferHeight();
        game_.renderWorld(viewport, false, false, false);

        // The interface of the game, over the world: the same canvas the
        // editor edits, drawn the same way. The one difference is the
        // pointer: in a game the buttons answer it.
        double pointerX = 0.0, pointerY = 0.0;
        game_.window().cursorPos(pointerX, pointerY);
        const Rect area{0.0f, 0.0f, (float)game_.window().framebufferWidth(),
                        (float)game_.window().framebufferHeight()};
        game_.renderGui(area, true, (float)pointerX, (float)pointerY,
                        game_.window().mouseDown(GLFW_MOUSE_BUTTON_LEFT),
                        game_.window().mousePressed(GLFW_MOUSE_BUTTON_LEFT),
                        game_.window().mouseReleased(GLFW_MOUSE_BUTTON_LEFT));

        // The picture of the screen, before the buffer is swapped.
        game_.takeScreenshotIfRequested();

        game_.window().present();

        // Everything this frame did not read dies with it.
        game_.window().dropUnreadInput();
    }
    return 0;
}

} // namespace s3d
