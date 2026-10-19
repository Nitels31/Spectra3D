#pragma once

// CMake only defines SPECTRA_SCRIPTING when the AngelScript option is on, so the
// default is what keeps this header compiling - and free of any AngelScript
// symbol - in a scripting-less build.
#ifndef SPECTRA_SCRIPTING
#define SPECTRA_SCRIPTING 0
#endif

#include <functional>
#include <string>
#include <vector>

#include "assets/MeshRegistry.hpp"
#include "audio/SoundDevice.hpp"
#include "physics/PhysicsWorld.hpp"
#include "render/Camera.hpp"
#include "platform/Window.hpp"

namespace s3d {
namespace script {

// AngelScript host for the `engine::` scripting layer.
//
// Two properties drive the whole design:
//
//  * One module per script. asIScriptModule::Build() is atomic - if any section
//    fails to compile the module ends up with zero functions - so sharing a
//    module would let one broken file delete the entry points of every other.
//    Module names come from the script path and a re-run replaces only its own.
//  * The world is never stepped here. Every binding mutates the live
//    PhysicsWorld in place, which is what makes the same script usable in EDIT
//    mode (batch authoring) and during PLAY.
//
// Entry points: after a successful build exactly ONE function of the module
// runs - the one named onInit (or main, or start) if the script declares it,
// otherwise the first global `void ()` in declaration order. Every other
// `void ()` is a helper the entry point is expected to call, so a helper next
// to an onInit builds its scene once instead of twice. Marking a helper
// `shared` keeps it out of the entry-point rule altogether, the same convention
// AngelScript uses for its own global functions.
//
// On top of that the host knows four optional hooks, each addressed by
// declaration through callFunction():
//
//   void onInit()              - runs once when the module is executed, as above;
//   void onUpdate(float dt)    - may be called once per rendered frame;
//   void onFixedUpdate(float)  - may be called once per fixed physics step;
//   void onStop()              - may be called once when play mode ends.
//
// All four are optional. A script that declares none of them is valid, and one
// that only declares `shared` helpers builds and runs nothing at all.
//
// **onUpdate is once per frame, onFixedUpdate is once per physics step.** Those
// are different numbers: the fixed step runs zero to kMaxStepsPerRenderedFrame
// times inside one frame, so a script that moves a body from onUpdate moves it
// at a rate that depends on the frame rate, and one that does it from
// onFixedUpdate gets the same answer on a 30 FPS laptop and a 144 Hz monitor.
// Visual work belongs in the first, simulation in the second, and putting
// something in the wrong one is a bug that only shows up on the machine that is
// not the one it was written on.
//
// The old no-argument `void onUpdate()` is still accepted, because every script
// written against it would otherwise stop updating the moment this shipped. It
// gets no dt, which is the honest thing to hand it.
class Host {
public:
    struct Diagnostic {
        std::string section;
        int row = 0, col = 0;
        std::string message;
        bool isError = false;
    };

    // Hook declarations as hasFunction() and callFunction() expect them. Exposed
    // so the editor can ask for a hook by name instead of repeating the strings.
    static constexpr const char* kOnInitDeclaration = "void onInit()";
    static constexpr const char* kOnUpdateDeclaration = "void onUpdate(float)";
    static constexpr const char* kOnUpdateLegacyDeclaration = "void onUpdate()";
    static constexpr const char* kOnFixedUpdateDeclaration = "void onFixedUpdate(float)";
    static constexpr const char* kOnStopDeclaration = "void onStop()";

    // `window` may be null, and is used for nothing but the input bindings: a
    // host with no window answers every input question with "no" and "zero",
    // which is what makes the headless scripttest possible without a display.
    // A script never sees the pointer, only asks it questions.
    //
    // `sound` is null when there is no device - a machine with no sound card -
    // and every sound binding then answers "no" instead of touching a device
    // that is not there.
    Host(PhysicsWorld& world, MeshRegistry& meshes, Camera& camera, Window* window,
         SoundDevice* sound = nullptr);
    ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    // Creates the engine and registers every binding. False if the engine itself
    // could not be created (diagnostics then explain why).
    bool initialize();
    void shutdown();

    // Receives every compiler message. Set before initialize() to catch them all.
    using DiagnosticSink = std::function<void(const Diagnostic&)>;
    void setDiagnosticSink(DiagnosticSink sink);

    // Compiles and runs. Returns false when compilation failed; the sink has
    // already been told. No-op returning false when scripting is not available.
    bool runString(const std::string& scriptName, const std::string& code);
    bool runFile(const std::string& path);

    // Not const, because answering it may have to go and look. The lookup is
    // cached, so a per-frame question is a vector walk and not a re-parse.
    bool hasFunction(const char* declaration);

    // Calls one global function by declaration, e.g.
    // callFunction(Host::kOnUpdateDeclaration). Safe to call every frame: a
    // declaration no loaded script has returns false WITHOUT logging, because an
    // absent optional hook is normal rather than a failure. A function that runs
    // and then fails reports through the sink and appends to diagnostics() - this
    // does not clear the diagnostics of the run that defined it.
    //
    // With `argument` (may be null) the call is a one-float call, which is what
    // the two per-frame hooks take. The float is a primitive, so it needs no
    // cleanup and cannot leave the context in a state Unprepare() would refuse.
    bool callFunction(const char* declaration, float argument = 0.0f);

    // The two per-frame hooks, called the way the frame loop calls them. Each
    // returns whether the script declared that hook at all, which is the caller's
    // cue to stop asking - a script with no onUpdate costs one cached lookup per
    // frame and nothing more.
    bool callUpdate(float dt);
    bool callFixedUpdate(float dt);
    bool callStop();

    const std::vector<Diagnostic>& diagnostics() const; // from the last run
    void clearDiagnostics();
    const std::string& engineVersion() const;
    bool available() const; // compiled in AND the engine exists

private:
    // One resolved declaration. `function` is null for a declaration that was
    // searched for and is not there - caching the misses is what makes a script
    // with no hooks cost a lookup instead of a full module sweep, per frame.
    struct Resolved {
        std::string declaration;
        void* function = nullptr;
    };

    // Resolves a declaration to a global function, searching the registered
    // `engine::` bindings first and every module's script globals second - the
    // engine's own lookup covers only the first of those, so the hooks a script
    // declares are invisible without the sweep. Returns null when nothing
    // matches, quietly. Typed as void* so this header still never names an
    // AngelScript symbol; the module a function belongs to owns it, so the
    // caller must not release it.
    void* resolveFunction(const char* declaration);

    // Compiles `code` as one module and runs its entry points.
    bool buildAndRun(const std::string& moduleName, const std::string& sectionName,
                     const std::string& code);

    // Runs one already-resolved global function in the host's single context;
    // `label` names the script in any diagnostic. Shared by the entry point and
    // by callFunction() so prepare/execute/report exists exactly once.
    // `function` is an asIScriptFunction*, which this header does not name.
    bool executeFunction(void* function, const char* label, float argument);

    // The one context, created on first use and kept until shutdown(). It is
    // unusable while a function is executing and is returned to a clean state by
    // Unprepare() afterwards, including after an exception - which is what lets a
    // per-frame hook reuse it instead of allocating one per call.
    void* context();

    // Drops the resolved-declaration cache. Every module a cached pointer could
    // have belonged to is either replaced or gone after a build, so a stale entry
    // is worse than no entry: it would call into a module that no longer exists.
    void clearFunctionCache();
    // A member because it reads and writes lastErrorKey_; see the .cpp. Takes void*
    // rather than asIScriptContext* because this header hides AngelScript's types.
    void emitExecutionFailure(void* ctx, const char* moduleName, const char* functionName);

    PhysicsWorld& world_;
    MeshRegistry& meshes_;
    Camera& camera_;
    Window* window_ = nullptr;
    SoundDevice* sound_ = nullptr;

    // asIScriptEngine* and asIScriptContext*, hidden so this header never has to
    // include AngelScript.
    void* engine_ = nullptr;
    void* context_ = nullptr;
    // Flat because there are a handful of entries and it is cleared wholesale; a
    // map would need a hash of a const char* this class does not own.
    std::vector<Resolved> functionCache_;
    // The identity of the last execution failure that was reported, so a hook that
    // throws on every call is logged once rather than once per call. A hook raising
    // the same exception at the same line 144 times a second would otherwise fill
    // the log ring buffer and push out the diagnostics that were actually useful.
    // Cleared on a success, so a hook that throws, recovers and throws again is
    // reported both times.
    std::string lastErrorKey_;
    DiagnosticSink sink_;
    std::vector<Diagnostic> diagnostics_;
    std::string version_;
};

} // namespace script
} // namespace s3d