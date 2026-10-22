#pragma once
#include <memory>
#include <string>
#include <vector>

#include "app/GameHost.hpp"
#include "app/Launcher.hpp"
#include "editor/Editor.hpp"
#include "render/UiRenderer.hpp"
#include "ui/UiContext.hpp"

namespace s3d {

// Composition root. Owns every long-lived subsystem and runs the frame loop;
// nothing below it reaches for global state.
//
// The application is a two-state machine: the launcher owns the window until a
// project is chosen, the editor owns it from then on.
//
// The game itself is a GameHost: the world, the picture of it, the sound and
// the scripts. The editor plays the game through it, and the player program -
// the program a build of a game is made of - is that and nothing else, so the
// two cannot drift apart.
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
    // The picture of the editor: the world in the viewport between the docks,
    // and the panels on top of it.
    void renderEditorScene();
    void updateTitle();
    // True while the editor's physics is stepping: the one case where the picture
    // changes without anyone touching anything.
    //
    // And while there are particles in the air, which is the other case: a fire
    // that nobody touches still burns, and a picture that changes on its own
    // needs frames - the window would draw one a second and the fire would
    // crawl.
    bool isSimulating() const {
        return (inEditor_ && editor_ && editor_->simulating()) ||
               game_.particles().hasLiveParticles();
    }

    // Hands the window over to the editor for `project`.
    void openProject(const ProjectInfo& project);
    void closeProject();

    void reportStartupInfo();

    // --- what File > Build does ---------------------------------------------
    // The editor decides where the build goes; copying the program, its runtime
    // files and the project into that folder, and starting the copy, is the
    // shell's job. Returns false with the reason in the log when it cannot.
    bool stageBuild(const std::string& target, bool alsoRun);
    // The models the project uses, and their pictures, staged into the build.
    int stageModels(const std::string& assets, const std::string& target,
                    const ProjectInfo& project);

    // The game: the world, the picture of it, the sound and the scripts.
    GameHost game_;
    // The launcher, and the interface it draws itself with.
    Launcher launcher_;
    UiContext launcherUi_;
    // The editor's own interface: the panels, the gizmos, the console.
    UiRenderer uiRenderer_;
    std::unique_ptr<Editor> editor_;

    bool inEditor_ = false;
};

} // namespace s3d
