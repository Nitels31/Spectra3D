#pragma once

// CMake only defines SPECTRA_SPSCRIPT when the Spectra3D Script option is on,
// so the default is what keeps this header compiling - and free of any
// Spectra3D Script symbol - in a build without that runtime.
#ifndef SPECTRA_SPSCRIPT
#define SPECTRA_SPSCRIPT 0
#endif

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "assets/MeshRegistry.hpp"
#include "audio/SoundDevice.hpp"
#include "physics/PhysicsWorld.hpp"
#include "platform/Window.hpp"
#include "render/Camera.hpp"
#include "render/ShadowMap.hpp"

namespace s3d {

class ParticleSystem;
namespace gui {
struct Canvas;
}

namespace sps {

// The host's own state. It is defined in the internal header, so this one
// stays free of any Spectra3D Script symbol.
struct HostData;

// The host of Spectra3D Script - the second script language of the engine,
// beside AngelScript.
//
// The two live side by side: a project may have .as files and .sps files at
// the same time, and each language sees the same world through its own
// bindings. Nothing of AngelScript is visible here and nothing of this is
// visible to AngelScript.
//
// **One script file is one context.** The runtime keeps the globals of the
// program that ran last and drops the rest, so two files in one context
// cannot both hold their own variables: the second file to run would take
// the first one's place. A context per file is what makes a project of
// several scripts work, and it is also what makes a broken script isolable -
// a script that fails takes its own context down and the others keep running.
//
// **The runtime does not recover from a run-time error.** Every later call in
// that context fails with the message of the first failure, and reading the
// globals of a context in that state is not safe. So a script that fails is
// marked broken, its hooks stop being called, and the only way back is a
// reload - which is what the editor's Reload action does.
//
// The hooks are the ones of the language:
//
//   def on_start()          - runs once, when the file has been loaded;
//   def on_update(delta)    - runs once per rendered frame;
//   def on_fixed_update(delta) - runs once per fixed physics step;
//   def on_stop()           - runs once when the play mode ends;
//   def on_destroy()        - runs once when the host shuts down.
//
// The camelCase spellings (onStart, onUpdate, onFixedUpdate, onStop,
// onDestroy) are accepted as well, because a file written for the other
// language of the engine should not have to be rewritten to be read.
//
// A hook that takes no argument is called with none: the runtime refuses a
// call with the wrong number of arguments, quietly, so the host asks with
// the argument first and without it second.
class Host {
public:
    struct Diagnostic {
        std::string section;
        int row = 0, col = 0;
        std::string message;
        bool isError = false;
    };

    using DiagnosticSink = std::function<void(const Diagnostic&)>;

    // `window` may be null, and is used for nothing but the input bindings: a
    // host with no window answers every input question with "no" and "zero",
    // which is what makes the headless spstest possible without a display.
    //
    // `sound` is null when there is no device - a machine with no sound card -
    // and every sound binding then answers quietly instead of touching a
    // device that is not there.
    //
    // `particles` is null when there is no particle system, and every particle
    // binding then answers quietly: a script that asks for a fire in a game
    // that has no particles is not a mistake worth a crash.
    //
    // `canvas` is null when there is no interface, and every interface binding
    // then answers quietly: a script that asks a label to change in a game
    // that has no labels is not a mistake worth a crash.
    //
    // `shadows` is null when there is no sun to configure, and the Light class
    // then reads zeros and writes nothing.
    Host(PhysicsWorld& world, MeshRegistry& meshes, Camera& camera, Window* window,
         SoundDevice* sound = nullptr, ParticleSystem* particles = nullptr,
         gui::Canvas* canvas = nullptr, ShadowSettings* shadows = nullptr);
    ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    // Creates the runtime and registers every binding. False when the runtime
    // could not be created (the diagnostics then explain why).
    bool initialize();
    void shutdown();

    // Receives every message of the compiler and of the runtime. Set before
    // initialize() to catch them all.
    void setDiagnosticSink(DiagnosticSink sink);

    // The folder of the project that is open. The data files of a script are
    // looked for in it, so a script says "level_01.spl" and the file is found
    // wherever the project keeps its data.
    void setProjectRoot(const std::string& root);

    // Compiles a file and runs it: the file's own context, its own globals,
    // and its entry point if it has one. Returns false when the file could not
    // be compiled; the sink has already been told.
    bool runFile(const std::string& path);

    // The same, from a source in memory. The name is what the diagnostics
    // call the script.
    bool runString(const std::string& scriptName, const std::string& code);

    // Compiles and runs every file that is loaded, from the start.
    //
    // This is the hot reload: the compiler is inside the runtime, so the
    // engine does the work itself and the scripts are running again a moment
    // after the file was saved. The contexts are made anew, so a script that
    // starts from a clean slate starts from a clean slate.
    //
    // The timers and the event handlers are dropped first: they name the
    // functions of the old scripts, and the new scripts have their own.
    bool reload();

    // Stops every script and forgets them: the contexts are freed and the
    // scripts are gone. What they left in the world stays there - the world
    // belongs to the editor, not to the scripts.
    void unload();

    // The two per-frame hooks, called the way the frame loop calls them.
    // Each returns whether any script declared that hook at all, which is the
    // caller's cue to stop asking.
    bool callUpdate(float dt);
    bool callFixedUpdate(float dt);
    bool callStop();
    bool callDestroy();

    // The number of the loaded scripts and the name of one of them.
    int scriptCount() const;
    std::string scriptName(int index) const;

    const std::vector<Diagnostic>& diagnostics() const; // from the last run
    void clearDiagnostics();
    const std::string& engineVersion() const;
    bool available() const; // compiled in AND the runtime exists

private:
    // Compiles a source and runs it as one script. `path` is where the source
    // came from, and it is what a reload reads again; an empty path means the
    // source is all there is.
    bool runSource(const std::string& name, const std::string& code, const std::string& path);

    std::unique_ptr<HostData> data_;
};

} // namespace sps
} // namespace s3d
