#pragma once

#ifndef SPECTRA_SCRIPTING
#define SPECTRA_SCRIPTING 0
#endif

#if SPECTRA_SCRIPTING
#include <angelscript.h>
#endif

#include <cstddef>
#include <string>
#include <vector>

// Bindings only ever hold pointers to these, so forward declarations are enough
// and this header stays cheap for the three Bind*.cpp files to include. They
// belong to s3d, not the global namespace - getting that wrong silently declares
// a second, unrelated type.
namespace s3d {
class PhysicsWorld;
class MeshRegistry;
struct Camera;
struct RigidBody;
class Window;
class SoundDevice;

namespace script {

// Everything the `engine::` bindings touch. The Host owns one instance and
// publishes its address while the engine is alive: AngelScript global functions
// carry no receiver, so the bindings resolve the live state on every call rather
// than caching a pointer that shutdown() could dangle.
struct ScriptState {
    PhysicsWorld* world = nullptr;
    MeshRegistry* meshes = nullptr;
    Camera* camera = nullptr;
    // The window, for the input bindings. Same deal as the other three: a
    // pointer the host keeps fresh, never one a script ever sees.
    Window* window = nullptr;
    // The sound device, for the sound bindings. Null when there is no device -
    // a machine with no sound card - and every sound binding then answers
    // "no" instead of touching a device that is not there.
    SoundDevice* sound = nullptr;

    // Script-authored groups. Members are body indices; removeBody() keeps them
    // in step with the erase it performs on the world, and clearScene() drops
    // them outright.
    std::vector<std::string> groupNames;
    std::vector<std::vector<int>> groups;

    // Idempotent by name, so a script that re-runs adds to its forest instead of
    // orphaning the previous one.
    int findOrCreateGroup(const std::string& name) {
        for (size_t i = 0; i < groupNames.size(); ++i)
            if (groupNames[i] == name) return (int)i;
        groupNames.push_back(name);
        groups.push_back({});
        return (int)groups.size() - 1;
    }
};

// Defined in ScriptHost.cpp. Null while no Host is initialised.
ScriptState* activeState();

// Shared guard for every index-taking binding: resolves the live world and the
// body, logging once and returning null when either is unusable. Nothing in the
// Bind*.cpp files dereferences a body index that did not come from here.
RigidBody* resolveBody(const char* fn, int index);

#if SPECTRA_SCRIPTING
// AngelScript has no built-in `string`, so this has to be registered before any
// declaration that mentions one. See BindString.hpp.
int registerStringType(asIScriptEngine* engine);

// One script-visible declaration and the C++ function behind it. They have to
// agree character for character: the only feedback for a mismatch is a
// registration failure when the engine starts.
struct Binding {
    const char* declaration;
    asSFuncPtr function;
};

// Registers in table order and stops at the first rejection, so a typo in one
// entry cannot be hidden by nine that happen to work. Returns asNO_MODULE; the
// engine has already sent its own message through the diagnostic sink.
int registerBindings(asIScriptEngine* engine, const Binding* bindings, size_t count);

// Registration entry points, one per translation unit. These all land in the
// script-visible `engine` namespace, which the host arranges once by pointing
// the engine's default namespace at it.
int registerEngineBindings(asIScriptEngine* engine);
int registerSceneBindings(asIScriptEngine* engine);
int registerCameraBindings(asIScriptEngine* engine);
int registerInputBindings(asIScriptEngine* engine);
int registerSoundBindings(asIScriptEngine* engine);
#endif

} // namespace script
} // namespace s3d