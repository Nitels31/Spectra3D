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

#include "../assets/MeshRegistry.hpp"
#include "../physics/PhysicsWorld.hpp"
#include "../render/Camera.hpp"

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
// On top of that the host knows three optional hooks, each addressed by
// declaration through callFunction():
//
//   void onInit()   - runs once when the module is executed, as above;
//   void onUpdate() - may be called every frame while play mode runs;
//   void onStop()   - may be called once when play mode ends.
//
// All three are optional. A script that declares none of them is valid, and one
// that only declares `shared` helpers builds and runs nothing at all.
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
    static constexpr const char* kOnUpdateDeclaration = "void onUpdate()";
    static constexpr const char* kOnStopDeclaration = "void onStop()";

    explicit Host(PhysicsWorld& world, MeshRegistry& meshes, Camera& camera);
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

    bool hasFunction(const char* declaration) const;

    // Calls one global function by declaration, e.g.
    // callFunction(Host::kOnUpdateDeclaration). Safe to call every frame: a
    // declaration no loaded script has returns false WITHOUT logging, because an
    // absent optional hook is normal rather than a failure. A function that runs
    // and then fails reports through the sink and appends to diagnostics() - this
    // does not clear the diagnostics of the run that defined it.
    bool callFunction(const char* declaration);

    const std::vector<Diagnostic>& diagnostics() const; // from the last run
    void clearDiagnostics();
    const std::string& engineVersion() const;
    bool available() const; // compiled in AND the engine exists

private:
    // Resolves a declaration to a global function, searching the registered
    // `engine::` bindings first and every module's script globals second - the
    // engine's own lookup covers only the first of those, so the hooks a script
    // declares are invisible without the sweep. Returns null when nothing
    // matches, quietly. Typed as void* so this header still never names an
    // AngelScript symbol; the module a function belongs to owns it, so the
    // caller must not release it.
    void* resolveFunction(const char* declaration) const;

    // Compiles `code` as one module and runs its entry points.
    bool buildAndRun(const std::string& moduleName, const std::string& sectionName,
                     const std::string& code);

    // Runs one already-resolved global function in a context of its own; `label`
    // names the script in any diagnostic. Shared by the entry point and by
    // callFunction() so prepare/execute/report exists exactly once. `function` is
    // an asIScriptFunction*, which this header does not name.
    bool executeFunction(void* function, const char* label);

    PhysicsWorld& world_;
    MeshRegistry& meshes_;
    Camera& camera_;

    // asIScriptEngine*, hidden so this header never has to include AngelScript.
    void* engine_ = nullptr;
    DiagnosticSink sink_;
    std::vector<Diagnostic> diagnostics_;
    std::string version_;
};

} // namespace script
} // namespace s3d