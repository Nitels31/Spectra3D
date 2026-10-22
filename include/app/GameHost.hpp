#pragma once
#include <string>
#include <vector>

#include "app/FixedClock.hpp"
#include "assets/MeshRegistry.hpp"
#include "assets/TextureRegistry.hpp"
#include "audio/SoundDevice.hpp"
#include "core/Project.hpp"
#include "gui/GuiCanvas.hpp"
#include "gui/GuiRenderer.hpp"
#include "i18n/Localizer.hpp"
#include "physics/PhysicsWorld.hpp"
#include "platform/Window.hpp"
#include "render/Camera.hpp"
#include "render/SceneRenderer.hpp"
#include "script/ScriptHost.hpp"

namespace s3d {

// The game, without a shell around it.
//
// The world, the picture of it, the sound, the scripts and the loop that runs
// them - everything a game is, and nothing that draws a dock. The editor plays
// the game through this, and the player program is this and nothing else, so
// the two cannot drift apart: the same world, the same camera, the same
// physics, the same scripts.
//
// The shell around it is the editor's business - the panels, the gizmos, the
// project list - and this class knows none of them.
class GameHost {
public:
    GameHost();
    ~GameHost();

    GameHost(const GameHost&) = delete;
    GameHost& operator=(const GameHost&) = delete;

    // The window, the renderer, the sky, the sound and the script host.
    // `title` is what the window says: the name of the game.
    bool initialize(const std::string& title);

    // The working directory and the log file: both belong next to the
    // program, and both have to be right before anything is read or written.
    //
    // A program that says something before the window is open - a player
    // without a game to play - says it here, in the log next to the program
    // and not only on the screen.
    static void adoptProgramFolder();

    // The world a game starts with: the gravity, the camera, the models, the
    // scene and the sounds of the project. The scripts are not run here - the
    // caller says when the game starts.
    void loadProject(const ProjectInfo& project);

    // Runs every script of the project. This is what "play" means.
    void startScripts();

    // --- the frame ----------------------------------------------------------
    //
    // The frame is in three parts, and the shell around this decides where
    // its own work goes: the camera first, then whatever the shell has to say
    // (the editor's panels, the game's own input), then the world.
    //
    // The camera is first because a click is answered with the camera the
    // user was looking through, and the world is last because the picture is
    // of the world as it is after the step.

    // The camera of the game: the scene's active camera, or the first camera
    // in the scene. -1 when the scene has none.
    int findGameCamera() const;

    // The first part of the frame: the camera of the game is picked and the
    // free camera moves. `running` says whether the game is running - the
    // editor stops it when the game is not playing, and a game always runs.
    void updateCamera(float dt, bool running);

    // The second part: the scripts and the physics. `simulate` says whether
    // the world is stepping.
    void updateWorld(float dt, bool simulate);

    // The picture of the world, in the rectangle given. The editor draws it
    // in the viewport between the docks; a game draws it in the whole window.
    void renderWorld(const Viewport& viewport, bool showGrid, bool showVectors,
                     bool showShadowDebug);

    // THE INTERFACE OF THE GAME, OVER THE WORLD.
    //
    // The canvas is part of the scene, so it is here with the scene: the game
    // draws it over the world, and the editor draws the same canvas over the
    // world it is editing.
    //
    // `area` is the rectangle it is drawn into, in window coordinates. The
    // pointer's state is passed in rather than asked for, because the editor
    // has already read it and the window's edges are read once per frame by
    // whoever reads them first.
    void renderGui(const Rect& area, bool interactive, float pointerX, float pointerY,
                   bool pointerDown, bool pointerPressed, bool pointerReleased);

    // The wait for the frame that is due, and the seconds since the last one.
    // False when there is nothing to draw: an idle window draws one frame a
    // second and no more.
    bool beginFrame(bool busy, float& dt);

    // The picture of the screen: asked for with F12 and taken here, at the
    // end of the frame and before the buffer is swapped - after the swap the
    // picture on the screen is the one that was there before.
    void takeScreenshotIfRequested();

    // Seconds between frames, or 0 when uncapped.
    double frameInterval() const;
    void setFrameRateCap(int fps) { frameRateCap_ = fps; }

    // How many threads the model loader may use.
    void setLoaderThreads(int threads);

    // --- the parts the shell around this needs -------------------------------
    Window& window() { return window_; }
    PhysicsWorld& world() { return world_; }
    MeshRegistry& meshes() { return meshes_; }
    TextureRegistry& textures() { return textures_; }
    SoundDevice& sound() { return sound_; }
    i18n::Localizer& localizer() { return localizer_; }
    Camera& camera() { return camera_; }
    SceneRenderer& sceneRenderer() { return sceneRenderer_; }
    // The particles of the world: the emitters, the pool and the drawing.
    // They belong to the renderer, because the renderer is what draws them.
    ParticleSystem& particles() { return sceneRenderer_.particles(); }
    const ParticleSystem& particles() const { return sceneRenderer_.particles(); }
    ShadowSettings& shadowSettings() { return shadowSettings_; }
    script::Host& scripts() { return scripts_; }
    const ProjectInfo& project() const { return project_; }
    ProjectInfo& project() { return project_; }

    // The interface of the game: the canvas of the scene and the thing that
    // draws it. The editor edits the canvas in place - the same one the game
    // draws - so what is on the screen while editing is what the game shows.
    gui::Canvas& canvas() { return canvas_; }
    const gui::Canvas& canvas() const { return canvas_; }
    gui::Renderer& guiRenderer() { return guiRenderer_; }

    // The body of the camera the game is looking through, or -1.
    int sceneCamera() const { return sceneCamera_; }
    // True while the camera entity is the view of the game.
    bool gameCameraInCharge() const { return gameCameraInCharge_; }
    // How far the physics is between two steps: the bodies are drawn where
    // they are and not where they were.
    float clockAlpha() const { return clockAlpha_; }
    // The time the world has been running, in seconds.
    double simulationTime() const { return simulationTime_; }
    void resetSimulationTime() { simulationTime_ = 0.0; }

private:
    // The sky the world is seen against: the engine's own, the project's, or
    // none at all.
    void loadSky();
    void loadSkyFrom(const std::string& directory);
    // The sky of the project, from its own `assets/sky`.
    void loadProjectSky(const std::string& projectAssets);

    // --- sound ---------------------------------------------------------------
    // Starts the device and loads the sounds that ship with the engine. A
    // machine with no sound card is a machine where everything else still has
    // to work, so a failure here is a line in the log and not a stop.
    void startAudio();
    // The sounds the project brings: loaded when the project is opened, after
    // the engine's, and they replace the ones of the same name.
    void loadProjectSounds();
    // The sounds the bodies make when they hit something, and the sounds the
    // scene's own bodies carry.
    void playImpactSounds();

    void updateCameraInput(float dt, bool allowMove);
    void updateSimulation(float dt, bool stepping);
    // Waits until `deadline` seconds have passed on the window clock, still
    // pumping the events so the window stays alive.
    bool waitForFrame(double deadline, bool wakeOnActivity);

    Window window_;
    PhysicsWorld world_;
    MeshRegistry meshes_;
    // Owned here rather than by the renderer: a texture outlives any one frame
    // and is shared by every body that uses the same image, so it belongs next
    // to the model registry and not inside the thing that draws.
    TextureRegistry textures_;
    // The sound of the world. Owned here, like the textures: a sound outlives
    // any one frame and is shared by every body that makes the same one.
    SoundDevice sound_;
    i18n::Localizer localizer_;
    Camera camera_;
    // The body index of the camera the game is looking through, or -1 when the
    // free camera is in charge. The body itself is not drawn while it is the
    // eye: its icon would be a hand's width from the near plane and would
    // fill the screen.
    int sceneCamera_ = -1;
    // True while the camera entity is the view of the game.
    bool gameCameraInCharge_ = false;
    SceneRenderer sceneRenderer_;
    // The shadows, as they are set right now. The editor changes them where
    // the shadows can be seen, and they are the project's - so they go into
    // the manifest with the rest of it.
    ShadowSettings shadowSettings_;
    script::Host scripts_;
    // The interface of the game. The canvas is scene data and the renderer is
    // the thing that draws it - the same pair the editor uses, so the editor
    // and the game cannot draw the interface differently.
    gui::Canvas canvas_;
    gui::Renderer guiRenderer_;

    ProjectInfo project_;

    // 60 Hz simulation, at most TWO steps per rendered frame.
    //
    // Two is FixedClock's own documented number, and it is not an arbitrary
    // safety limit: it is what keeps frame time proportional to the cost of
    // a step. `dt` is clamped to kMaxFrameTime, so a fully clamped frame can
    // fund six steps, and paying for six is what pins the frame rate to a
    // plateau a slower step cannot leave - because clearing the accumulator
    // throws the debt away instead of paying it down. See FixedClock.hpp.
    FixedClock clock_{60.0, 2};
    // How far the physics accumulator is into its next step, read by renderWorld
    // so bodies are drawn interpolated between fixed steps rather than at them.
    // 1 until the first step says otherwise, because 1 is "draw the transform the
    // world actually has" and that is the right answer whenever nothing has moved.
    float clockAlpha_ = 1.0f;
    // The impulse each body took in the last step, one entry per body. Kept
    // here so that a step does not allocate: the list is as long as the world
    // and the world does not change size every frame.
    std::vector<float> impactImpulses_;
    double simulationTime_ = 0.0;

    // The cap on the frames per second, 0 for none.
    int frameRateCap_ = 60;
    // Start of the current one-second window, on the window clock.
    double lastTickTime_ = 0.0;
    double previousTime_ = 0.0;
    // When the next capped frame is due, on the window clock. Absolute, so the
    // period does not drift with the render time.
    double nextFrameTime_ = 0.0;
    // True on the frame the user asked for a picture of the screen.
    bool screenshotRequested_ = false;
    // How many threads the model loader may use.
    int loaderThreads_ = 4;
};

} // namespace s3d
