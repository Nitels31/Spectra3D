#pragma once

#ifndef SPECTRA_SPSCRIPT
#define SPECTRA_SPSCRIPT 0
#endif

#if SPECTRA_SPSCRIPT
// The SDK's own header declares sps_version() without the extern "C" that every
// other declaration in it has, so the whole header is wrapped here instead of
// the one function being renamed around. Nesting extern "C" is legal, and the
// other declarations are already inside one.
extern "C" {
#include "sps/sps.h"
}
#endif

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/Math.hpp"
#include "render/Color.hpp"

#include "script/SpsHost.hpp"

namespace s3d {

class MeshRegistry;
class ParticleSystem;
class SoundDevice;
class Window;
struct Camera;
struct ShadowSettings;

namespace gui {
struct Canvas;
}

namespace sps {

// --- the values the engine hands to the scripts ------------------------------
//
// Every object a script holds is a pointer into a pool the host owns, and the
// runtime never owns one of them.
//
// The runtime frees a host object when its context dies, and it frees it once
// per VALUE that refers to it - a script that copies an object copies the
// pointer, so an object that two values share would be freed twice and the
// second free would be on memory that is already gone. A pool the host owns
// has neither problem: nothing is freed by the runtime, and the host lets go
// of the pool when it shuts down.

// The number of the engine a script object points at.
//
// The tag is what tells one kind of script object from another: a script can
// hand any object to any function, and a value that is not an entity must not
// be read as one.
struct EntityValue {
    static constexpr unsigned kTag = 0x5350454Eu; // "SPEN"
    int body = -1;
    unsigned tag = kTag;
};

// The values that are made and thrown away: a vector, a colour. They come out
// of a ring, because a script that asks for one every frame would otherwise
// fill the memory of the process with values nobody ever reads again.
//
// The ring is long enough that a script has to keep a value alive across
// thousands of calls before the ring comes back to it - which is longer than
// any game keeps a number.
constexpr int kValuePoolSize = 4096;

// --- the shared state --------------------------------------------------------

// Everything the bindings of every script touch.
//
// It is shared because the world is shared: a body that one script made is a
// body the next script can find by name, and a sound that one script plays is
// a sound the next script can stop.
struct SpsState {
    PhysicsWorld* world = nullptr;
    MeshRegistry* meshes = nullptr;
    Camera* camera = nullptr;
    Window* window = nullptr;
    SoundDevice* sound = nullptr;
    ParticleSystem* particles = nullptr;
    gui::Canvas* canvas = nullptr;
    ShadowSettings* shadows = nullptr;

    // The names and the tags the scripts gave to the bodies of the world, one
    // entry per body. A body the editor made has no name until a script gives
    // it one.
    std::vector<std::string> bodyNames;
    std::vector<std::string> bodyTags;

    // The entities the scripts made. A deque, because a script keeps a pointer
    // to one of them and a vector would move it when the next one is made.
    std::deque<EntityValue> entities;

    // A sound of the game: what it is called, how loud it is and how fast it
    // plays. The sounds themselves belong to the device; this is the part of
    // a sound that a script changes.
    struct AudioValue {
        std::string sound;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
    };
    std::deque<AudioValue> audio;

    // The pieces of the interface the scripts made: the index of the element
    // in the canvas.
    std::deque<int> elements;

    // The ring of the values that are made and thrown away.
    std::vector<Vec3> vectors;
    std::vector<Color> colors;
    int vectorCursor = 0;
    int colorCursor = 0;

    // A trigger: a body of the world, the name of the function to call when
    // something comes in and when it goes out, and the bodies that are inside
    // it right now.
    struct Trigger {
        int body = -1;
        std::string onEnter;
        std::string onExit;
        bool enabled = true;
        std::vector<int> inside;
    };
    std::vector<Trigger> triggers;

    // The events: a name and the names of the functions that listen to it.
    std::map<std::string, std::vector<std::string>> handlers;

    // The timers of the scripts: a function to call and how often.
    struct Timer {
        int id = 0;
        std::string function;
        double remaining = 0.0;
        double interval = 0.0;
        bool repeat = false;
    };
    std::vector<Timer> timers;
    int nextTimerId = 1;

    // The bodies the scripts made, in the order they made them. A reload
    // takes them out again, so a script that builds a scene builds it once
    // and not twice.
    std::vector<int> created;

    // How many times the runtime has reported an error. A call that fails
    // WITHOUT one of them was a call with the wrong number of arguments,
    // which the runtime refuses quietly - and that is how the host tells the
    // two apart.
    int errorCount = 0;

    // The frames the host has run and the seconds they have taken. The
    // scripts ask for them through frame() and time().
    int frame = 0;
    double time = 0.0;
    // The length of the last frame, in seconds.
    float deltaTime = 0.0f;

    // The folder of the project that is open. The data files of a script are
    // looked for in it, so a script says "level_01.spl" and the file is found
    // wherever the project keeps its data.
    std::string projectRoot;
};

// What an event carries.
//
// A number or a word means the same thing in every script, so it is carried
// as it is. A body is a number of the world, and every script makes its own
// object out of it - the classes are per script, so an object of one script
// is not an object of another.
struct Payload {
    SpsValue value = sps_nil();
    int body = -1;
};

// --- one script --------------------------------------------------------------

// A loaded script: its own context, its own globals, its own classes.
//
// The classes are per context because the runtime keeps its class table per
// context, and the values the classes hand out are per host because the world
// is per host.
struct Script {
    std::string path;
    // The code the script was run with. A reload runs it again when there is
    // no file to read it from.
    std::string source;
    std::string name;
    SpsContext* context = nullptr;
    // The compiled script. The globals of a script live in it, so it has to
    // outlive them: the chunk is freed before the context and never after.
    SpsChunk* chunk = nullptr;
    SpsState* state = nullptr;
    HostData* host = nullptr;

    // True when the script failed at run time. The runtime cannot recover from
    // that: every later call in its context fails with the message of the
    // first failure, and reading the globals of such a context is not safe.
    // So the script is left alone, and a reload is the way back.
    bool broken = false;

    // The hooks, looked up once when the script was loaded. A hook that is
    // not there is nil, and nil is not a function.
    SpsValue onUpdate{};
    SpsValue onFixedUpdate{};
    SpsValue onStop{};
    SpsValue onDestroy{};

    // The classes of this script.
    SpsHostClass* vectorClass = nullptr;
    SpsHostClass* colorClass = nullptr;
    SpsHostClass* entityClass = nullptr;
    SpsHostClass* sceneClass = nullptr;
    SpsHostClass* rigidbodyClass = nullptr;
    SpsHostClass* colliderClass = nullptr;
    SpsHostClass* cameraClass = nullptr;
    SpsHostClass* lightClass = nullptr;
    SpsHostClass* materialClass = nullptr;
    SpsHostClass* triggerClass = nullptr;
    SpsHostClass* audioClass = nullptr;
    SpsHostClass* labelClass = nullptr;
    SpsHostClass* buttonClass = nullptr;
};

// --- the host's insides ------------------------------------------------------

// The host's own state. The Bind*.cpp files reach it through the script they
// belong to, so nothing here has to be a global - the AngelScript host needs
// one because its global functions carry no receiver, and the functions of
// this runtime carry their context and their user pointer.
struct HostData {
    SpsState state;

    // The scripts, each one behind a pointer: the runtime's error handler
    // carries the script it belongs to as a pointer, and a vector would move
    // its scripts every time another one is loaded.
    std::vector<std::unique_ptr<Script>> scripts;

    Host::DiagnosticSink sink;
    std::vector<Host::Diagnostic> diagnostics;
    std::string version;
    bool available = false;
};

// --- the helpers the bindings share ------------------------------------------

// The text of a value, for a log line.
//
// The runtime's own text of a value is not safe for every value: asked for
// the text of a function it does not come back. So the text is made here, and
// a value that has no text of its own is named by its type.
std::string textOf(SpsContext* context, SpsValue value);

// The body a script object points at, or null when it points at nothing.
RigidBody* bodyOf(SpsState& state, int index);

// The index of a body, or -1 when the value is not an entity.
int entityIndexOf(SpsValue value);

// The handle of an entity, made if there is none yet: a body that the editor
// made has no handle until a script asks for one.
EntityValue* entityValueOf(SpsState& state, int body);

// A vector out of the ring.
SpsValue makeVector(Script& script, const Vec3& value);

// A colour out of the ring.
SpsValue makeColor(Script& script, const Color& value);

// The vector a value points at, or the zero vector.
Vec3 vectorOf(SpsValue value);

// The colour a value points at, or white.
Color colorOf(SpsValue value);

// An entity the host owns. The caller keeps it: the pool is the host's and
// the host lets go of it when it shuts down.
SpsValue makeEntity(Script& script, int body);

// The same, but as an object of the class the caller names: the Rigidbody of
// a body and the Collider of the same body are two kinds of object that point
// at one body.
SpsValue makeEntityOf(Script& script, SpsHostClass* klass, int body);

// Takes a body out of the world and puts everything that pointed at it back
// in order: the entities, the names, the tags and the list of the bodies the
// scripts made.
void removeBody(SpsState& state, int index);

// Empties the world: the bodies, the names, the tags, the triggers and the
// list of the bodies the scripts made.
void clearScene(SpsState& state);

// Makes the tables of the names and of the tags as long as the world is, so
// a body that the editor made has a place in them too.
void ensureBodyTables(SpsState& state);

// Reports a message of the host: to the sink, to the diagnostics of the last
// run and to the log.
void report(HostData& host, bool isError, const std::string& section, int row, int col,
            const std::string& message);

// Calls a function by name in every script that has it, with a payload.
void emitEvent(Script& script, const std::string& name, const Payload& payload);

// Calls a function by name in every script that has it, with the arguments
// the caller gave. The result of the first script that answered is the
// result of the call.
SpsValue callFunctionByName(Script& script, const std::string& name, int argc,
                            const SpsValue* args);

// The registration of the classes and of the functions of one script.
bool registerScript(Script& script);

// The registration of the classes.
bool registerClasses(Script& script);

// The registration of the functions.
bool registerFunctions(Script& script);
} // namespace sps
} // namespace s3d
