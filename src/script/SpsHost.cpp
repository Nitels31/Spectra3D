#include "script/SpsHost.hpp"

#include "script/SpsBindings.hpp"

#include "core/Log.hpp"
#include "core/Str.hpp"
#include "platform/Filesystem.hpp"

#if SPECTRA_SPSCRIPT
#include "gui/GuiCanvas.hpp"
#include "physics/PhysicsWorld.hpp"
#endif

#include <algorithm>

namespace s3d {
namespace sps {

#if SPECTRA_SPSCRIPT
namespace {

// The names of the hooks, in the order they are tried.
//
// The snake_case spellings are the ones of the language; the camelCase ones
// are there so that a file written for the other language of the engine does
// not have to be rewritten to be read.
const char* const kEntryNames[] = {"on_start", "onStart", "on_init", "onInit", "main", "start"};
const char* const kUpdateNames[] = {"on_update", "onUpdate"};
const char* const kFixedUpdateNames[] = {"on_fixed_update", "onFixedUpdate"};
const char* const kStopNames[] = {"on_stop", "onStop"};
const char* const kDestroyNames[] = {"on_destroy", "onDestroy"};

// The runtime's error handler. The script it belongs to travels with it as the
// user pointer, so a failure is reported against the file that failed and
// against no other.
void onRuntimeError(SpsContext* context, const char* message, const char* stack, void* user) {
    (void)context;
    Script* script = static_cast<Script*>(user);
    if (!script) return;
    if (script->state) ++script->state->errorCount;

    std::string text = message ? message : "the script failed";
    if (stack && *stack) text += "\n" + std::string(stack);
    if (script->host) report(*script->host, true, script->name, 0, 0, text);
}

// The first of the names that the script has as a function.
SpsValue findFunction(Script& script, const char* const* names, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        SpsValue value = sps_context_get_global(script.context, names[i]);
        if (value.type == SPS_FUNCTION) return value;
    }
    return sps_nil();
}

// Calls a function of the script, with the number of arguments it takes.
//
// The runtime refuses a call with the wrong number of arguments, and it does
// so quietly: no error, no message. So the call is made with the argument
// first, and if it fails without an error it is made again without one - a
// hook that takes no argument is a hook that takes no argument.
//
// The error count is what tells the two failures apart: a call that failed
// because of the arguments did not make the count grow, and a call that failed
// because of the script did.
bool callWith(Script& script, SpsValue function, const SpsValue* argument) {
    if (function.type != SPS_FUNCTION || !script.context) return false;

    SpsState& state = *script.state;
    const int errorsBefore = state.errorCount;

    SpsValue result;
    if (argument) {
        if (sps_call(script.context, function, 1, argument, &result)) return true;
        if (state.errorCount != errorsBefore) return false;
    }
    return sps_call(script.context, function, 0, nullptr, &result);
}

// Calls a function by name in every script that has it.
//
// The name is looked up in each script in turn, because every script has its
// own globals: a name is not a function until the script that has it is
// found. A body that comes with the call is turned into an entity of the
// script that is called, because the classes are per script too.
void callNamedAll(HostData& host, const std::string& name, const Payload& payload) {
    for (const std::unique_ptr<Script>& script : host.scripts) {
        if (script->broken || !script->context) continue;

        SpsValue function = sps_context_get_global(script->context, name.c_str());
        if (function.type != SPS_FUNCTION) continue;

        SpsValue argument =
            payload.body >= 0 ? makeEntity(*script, payload.body) : payload.value;

        const int errorsBefore = script->state->errorCount;
        callWith(*script, function, &argument);
        if (script->state->errorCount != errorsBefore) script->broken = true;
    }
}

// The boxes of two bodies, as the world measures them.
struct Box {
    Vec3 low;
    Vec3 high;
};

Box boxOf(const RigidBody& body) {
    Box box;
    box.low = body.position - body.halfExtents;
    box.high = body.position + body.halfExtents;
    return box;
}

bool boxesOverlap(const Box& a, const Box& b) {
    return a.low.x <= b.high.x && a.high.x >= b.low.x && a.low.y <= b.high.y &&
           a.high.y >= b.low.y && a.low.z <= b.high.z && a.high.z >= b.low.z;
}

// The triggers: a body of the world with a name of a function on each side.
// A body that comes in is told once, and a body that goes out is told once.
void updateTriggers(HostData& host) {
    SpsState& state = host.state;
    if (!state.world || state.triggers.empty()) return;

    for (SpsState::Trigger& trigger : state.triggers) {
        if (!trigger.enabled) continue;
        RigidBody* volume = state.world->find(trigger.body);
        if (!volume) {
            trigger.inside.clear();
            continue;
        }
        const Box area = boxOf(*volume);

        std::vector<int> inside;
        for (size_t i = 0; i < state.world->bodies.size(); ++i) {
            if ((int)i == trigger.body) continue;
            const RigidBody& body = state.world->bodies[i];
            if (!body.visible) continue;
            if (!boxesOverlap(area, boxOf(body))) continue;
            inside.push_back((int)i);
        }

        for (int index : inside) {
            const bool wasInside = std::find(trigger.inside.begin(), trigger.inside.end(),
                                             index) != trigger.inside.end();
            if (wasInside || trigger.onEnter.empty()) continue;
            Payload payload;
            payload.body = index;
            callNamedAll(host, trigger.onEnter, payload);
        }
        for (int index : trigger.inside) {
            const bool stillInside =
                std::find(inside.begin(), inside.end(), index) != inside.end();
            if (stillInside || trigger.onExit.empty()) continue;
            Payload payload;
            payload.body = index;
            callNamedAll(host, trigger.onExit, payload);
        }
        trigger.inside = inside;
    }
}

// The buttons of the interface: the canvas says which one was pressed, and
// the script that gave the button its name is told.
void updateButtons(HostData& host) {
    SpsState& state = host.state;
    if (!state.canvas) return;

    for (gui::Element& element : state.canvas->elements) {
        if (element.kind != gui::ElementKind::Button) continue;
        if (!element.clicked) continue;
        if (element.name.empty()) continue;
        callNamedAll(host, element.name, Payload());
    }
}

// The timers of the scripts: a function to call, and how long until it is
// called again.
void updateTimers(HostData& host, float dt) {
    SpsState& state = host.state;
    if (state.timers.empty()) return;

    std::vector<int> due;
    for (size_t i = 0; i < state.timers.size(); ++i) {
        SpsState::Timer& timer = state.timers[i];
        timer.remaining -= (double)dt;
        if (timer.remaining > 0.0) continue;
        if (timer.repeat) {
            timer.remaining += timer.interval;
            if (timer.remaining < 0.0) timer.remaining = timer.interval;
        } else {
            due.push_back(timer.id);
        }
    }
    for (int id : due) {
        std::string function;
        for (const SpsState::Timer& timer : state.timers) {
            if (timer.id != id) continue;
            function = timer.function;
            break;
        }
        state.timers.erase(std::remove_if(state.timers.begin(), state.timers.end(),
                                          [&](const SpsState::Timer& timer) {
                                              return timer.id == id;
                                          }),
                           state.timers.end());
        if (!function.empty()) callNamedAll(host, function, Payload());
    }
}

// Takes the bodies the scripts made out of the world, so a script that builds
// a scene builds it once and not twice.
void takeDownCreatedBodies(SpsState& state) {
    if (!state.world || state.created.empty()) return;

    std::vector<int> indices = state.created;
    std::sort(indices.begin(), indices.end(), std::greater<int>());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

    for (int index : indices) {
        if (index < 0 || index >= (int)state.world->bodies.size()) continue;
        removeBody(state, index);
    }
    state.created.clear();
}

} // namespace
#endif

// --- the calls into the scripts ----------------------------------------------

void emitEvent(Script& script, const std::string& name, const Payload& payload) {
    if (!script.host) return;
    callNamedAll(*script.host, name, payload);
}

SpsValue callFunctionByName(Script& script, const std::string& name, int argc,
                            const SpsValue* args) {
    if (!script.host) return sps_nil();

    SpsValue result = sps_nil();
    for (const std::unique_ptr<Script>& other : script.host->scripts) {
        if (other->broken || !other->context) continue;

        SpsValue function = sps_context_get_global(other->context, name.c_str());
        if (function.type != SPS_FUNCTION) continue;

        SpsValue called;
        const int errorsBefore = other->state->errorCount;
        if (argc > 0 && args)
            sps_call(other->context, function, argc, args, &called);
        else
            sps_call(other->context, function, 0, nullptr, &called);
        if (other->state->errorCount != errorsBefore) {
            other->broken = true;
            continue;
        }
        result = called;
        break;
    }
    return result;
}

// --- the helpers the bindings share ------------------------------------------

std::string textOf(SpsContext* context, SpsValue value) {
    if (value.type == SPS_FUNCTION || value.type == SPS_CLASS) {
        // The runtime's own text of a function is not safe to ask for.
        return std::string(sps_type_name(value.type));
    }
    const char* text = sps_value_to_text(context, value);
    return text ? std::string(text) : std::string();
}

RigidBody* bodyOf(SpsState& state, int index) {
    if (!state.world || index < 0) return nullptr;
    return state.world->find(index);
}

int entityIndexOf(SpsValue value) {
    if (value.type != SPS_OBJECT && value.type != SPS_USERDATA) return -1;
    const EntityValue* entity = static_cast<const EntityValue*>(sps_value_to_userdata(value));
    if (!entity || entity->tag != EntityValue::kTag) return -1;
    return entity->body;
}

EntityValue* entityValueOf(SpsState& state, int body) {
    for (EntityValue& entity : state.entities)
        if (entity.body == body) return &entity;
    state.entities.push_back(EntityValue{body, EntityValue::kTag});
    return &state.entities.back();
}

SpsValue makeVector(Script& script, const Vec3& value) {
    SpsState& state = *script.state;
    if (state.vectors.empty()) state.vectors.resize(kValuePoolSize);
    Vec3& slot = state.vectors[state.vectorCursor];
    slot = value;
    state.vectorCursor = (state.vectorCursor + 1) % kValuePoolSize;
    return sps_host_object(script.vectorClass, &slot);
}

SpsValue makeColor(Script& script, const Color& value) {
    SpsState& state = *script.state;
    if (state.colors.empty()) state.colors.resize(kValuePoolSize);
    Color& slot = state.colors[state.colorCursor];
    slot = value;
    state.colorCursor = (state.colorCursor + 1) % kValuePoolSize;
    return sps_host_object(script.colorClass, &slot);
}

Vec3 vectorOf(SpsValue value) {
    if (value.type != SPS_OBJECT && value.type != SPS_USERDATA) return Vec3();
    const Vec3* vector = static_cast<const Vec3*>(sps_value_to_userdata(value));
    return vector ? *vector : Vec3();
}

Color colorOf(SpsValue value) {
    if (value.type != SPS_OBJECT && value.type != SPS_USERDATA) return Color();
    const Color* color = static_cast<const Color*>(sps_value_to_userdata(value));
    return color ? *color : Color();
}

SpsValue makeEntity(Script& script, int body) {
    SpsState& state = *script.state;
    return sps_host_object(script.entityClass, entityValueOf(state, body));
}

SpsValue makeEntityOf(Script& script, SpsHostClass* klass, int body) {
    if (!klass) return sps_nil();
    SpsState& state = *script.state;
    return sps_host_object(klass, entityValueOf(state, body));
}

void removeBody(SpsState& state, int index) {
    if (!state.world || index < 0 || index >= (int)state.world->bodies.size()) return;

    state.world->remove(index);

    // Everything that pointed at a body above the one that is gone has to
    // move down by one, or it points at a body that is not there any more.
    for (EntityValue& entity : state.entities)
        if (entity.body > index) --entity.body;

    if (index < (int)state.bodyNames.size()) {
        state.bodyNames.erase(state.bodyNames.begin() + index);
        state.bodyTags.erase(state.bodyTags.begin() + index);
    }
    for (SpsState::Trigger& trigger : state.triggers) {
        if (trigger.body > index) --trigger.body;
        for (int& inside : trigger.inside)
            if (inside > index) --inside;
    }
    for (int& created : state.created) {
        if (created == index) {
            created = -1;
        } else if (created > index) {
            --created;
        }
    }
    state.created.erase(std::remove(state.created.begin(), state.created.end(), -1),
                        state.created.end());
}

void clearScene(SpsState& state) {
    if (!state.world) return;
    state.world->clear();
    state.bodyNames.clear();
    state.bodyTags.clear();
    state.triggers.clear();
    state.created.clear();
    for (EntityValue& entity : state.entities) entity.body = -1;
}

void ensureBodyTables(SpsState& state) {
    if (!state.world) return;
    const size_t count = state.world->bodies.size();
    if (state.bodyNames.size() < count) state.bodyNames.resize(count);
    if (state.bodyTags.size() < count) state.bodyTags.resize(count);
}

void report(HostData& host, bool isError, const std::string& section, int row, int col,
            const std::string& message) {
    Host::Diagnostic diagnostic;
    diagnostic.section = section;
    diagnostic.row = row;
    diagnostic.col = col;
    diagnostic.message = message;
    diagnostic.isError = isError;

    if (host.sink) host.sink(diagnostic);
    host.diagnostics.push_back(diagnostic);

    const char* where = section.empty() ? "sps" : section.c_str();
    if (isError)
        Log::error("%s(%d,%d): %s", where, row, col, message.c_str());
    else
        Log::warn("%s(%d,%d): %s", where, row, col, message.c_str());
}

// --- the host ----------------------------------------------------------------

Host::Host(PhysicsWorld& world, MeshRegistry& meshes, Camera& camera, Window* window,
           SoundDevice* sound, ParticleSystem* particles, gui::Canvas* canvas,
           ShadowSettings* shadows)
    : data_(new HostData()) {
    data_->state.world = &world;
    data_->state.meshes = &meshes;
    data_->state.camera = &camera;
    data_->state.window = window;
    data_->state.sound = sound;
    data_->state.particles = particles;
    data_->state.canvas = canvas;
    data_->state.shadows = shadows;
}

Host::~Host() { shutdown(); }

bool Host::initialize() {
#if SPECTRA_SPSCRIPT
    if (data_->available) return true;

    data_->version = "Spectra3D Script ";
    data_->version += std::to_string(SPS_VERSION_MAJOR) + "." +
                      std::to_string(SPS_VERSION_MINOR) + "." +
                      std::to_string(SPS_VERSION_PATCH);
    data_->available = true;

    // The ring of the values that are made and thrown away.
    data_->state.vectors.resize(kValuePoolSize);
    data_->state.colors.resize(kValuePoolSize);

    Log::info("Scripting engine %s ready", data_->version.c_str());
    return true;
#else
    report(*data_, true, "", 0, 0, "built without Spectra3D Script (SPECTRA_SPSCRIPT=OFF)");
    return false;
#endif
}

void Host::shutdown() {
#if SPECTRA_SPSCRIPT
    unload();
    data_->available = false;
    data_->version.clear();
#endif
}

void Host::setDiagnosticSink(DiagnosticSink sink) { data_->sink = std::move(sink); }

void Host::setProjectRoot(const std::string& root) { data_->state.projectRoot = root; }

bool Host::runFile(const std::string& path) {
#if SPECTRA_SPSCRIPT
    if (!data_->available) return false;

    std::string code;
    if (!fs::readFile(path, code)) {
        report(*data_, true, path, 0, 0, "the script file could not be read");
        return false;
    }
    return runSource(path, code, path);
#else
    (void)path;
    return false;
#endif
}

bool Host::runString(const std::string& scriptName, const std::string& code) {
    return runSource(scriptName, code, std::string());
}

bool Host::runSource(const std::string& name, const std::string& code,
                     const std::string& path) {
#if SPECTRA_SPSCRIPT
    if (!data_->available) return false;

    std::unique_ptr<Script> script(new Script());
    script->name = name.empty() ? std::string("script") : name;
    script->source = code;
    script->path = path;
    script->state = &data_->state;
    script->host = data_.get();

    script->context = sps_context_new();
    if (!script->context) {
        report(*data_, true, name, 0, 0, "the runtime could not create a context");
        return false;
    }
    sps_context_set_error_handler(script->context, onRuntimeError, script.get());

    if (!registerScript(*script)) {
        sps_context_free(script->context);
        return false;
    }

    SpsChunk* chunk = sps_compile(script->context, code.c_str(), name.c_str());
    if (!chunk) {
        int count = 0;
        const SpsDiagnostic* diagnostics = sps_compiler_diagnostics(script->context, &count);
        for (int i = 0; i < count; ++i) {
            const std::string file = diagnostics[i].file[0] ? diagnostics[i].file : name;
            report(*data_, true, file, diagnostics[i].line, diagnostics[i].column,
                   diagnostics[i].message);
        }
        if (count == 0)
            report(*data_, true, name, 0, 0, sps_context_last_error(script->context));
        sps_context_free(script->context);
        return false;
    }

    const int errorsBefore = data_->state.errorCount;
    const bool ran = sps_context_run(script->context, chunk);
    if (!ran || data_->state.errorCount != errorsBefore) {
        // The runtime cannot go on with a context that has failed, so the
        // context goes and the script is left broken.
        script->broken = true;
        sps_chunk_free(script->context, chunk);
        sps_context_free(script->context);
        script->context = nullptr;
        script->chunk = nullptr;
        return false;
    }

    // The chunk stays with the script: the globals of the script live in it,
    // and a program whose globals are gone is a program that cannot be asked
    // anything.
    script->chunk = chunk;

    // The hooks, looked up once: a script that has no hook costs a lookup
    // when it is loaded and nothing at all per frame.
    script->onUpdate = findFunction(*script, kUpdateNames, 2);
    script->onFixedUpdate = findFunction(*script, kFixedUpdateNames, 2);
    script->onStop = findFunction(*script, kStopNames, 2);
    script->onDestroy = findFunction(*script, kDestroyNames, 2);

    data_->scripts.push_back(std::move(script));

    // The entry point: the function the script starts with. A script without
    // one is a script that only has helpers, and it is a valid script.
    Script& loaded = *data_->scripts.back();
    SpsValue entry = findFunction(loaded, kEntryNames, 6);
    if (entry.type == SPS_FUNCTION) {
        const int before = data_->state.errorCount;
        SpsValue result;
        sps_call(loaded.context, entry, 0, nullptr, &result);
        if (data_->state.errorCount != before) loaded.broken = true;
    }
    return true;
#else
    (void)name;
    (void)code;
    (void)path;
    return false;
#endif
}

bool Host::reload() {
#if SPECTRA_SPSCRIPT
    if (!data_->available) return false;

    // What was loaded, and where it came from. A script that was run from a
    // file is read again - that is the whole point of the reload - and a
    // script that was run from a source is run again from that source.
    struct Loaded {
        std::string name;
        std::string path;
        std::string source;
    };
    std::vector<Loaded> loaded;
    loaded.reserve(data_->scripts.size());
    for (const std::unique_ptr<Script>& script : data_->scripts)
        loaded.push_back(Loaded{script->name, script->path, script->source});

    // The bodies the scripts made go away first: a script that builds a scene
    // builds it once and not twice.
    takeDownCreatedBodies(data_->state);

    unload();

    bool all = true;
    for (const Loaded& item : loaded) {
        std::string code = item.source;
        if (!item.path.empty() && fs::exists(item.path)) {
            std::string text;
            if (fs::readFile(item.path, text)) code = text;
        }
        if (!runSource(item.name, code, item.path)) all = false;
    }
    return all;
#else
    return false;
#endif
}

void Host::unload() {
#if SPECTRA_SPSCRIPT
    // The timers and the event handlers name the functions of the scripts that
    // are going away, so they go with them.
    data_->state.timers.clear();
    data_->state.handlers.clear();
    data_->state.triggers.clear();

    for (const std::unique_ptr<Script>& script : data_->scripts) {
        if (!script->context) continue;
        // The chunk first: the globals of the script live in it, and the
        // context is freed after it and not before.
        if (script->chunk) {
            sps_chunk_free(script->context, script->chunk);
            script->chunk = nullptr;
        }
        sps_context_free(script->context);
        script->context = nullptr;
    }
    data_->scripts.clear();
    data_->state.created.clear();
#endif
}

bool Host::callUpdate(float dt) {
#if SPECTRA_SPSCRIPT
    data_->state.time += (double)dt;
    data_->state.deltaTime = dt;
    ++data_->state.frame;

    bool any = false;
    for (const std::unique_ptr<Script>& script : data_->scripts) {
        if (script->broken || !script->context) continue;
        if (script->onUpdate.type != SPS_FUNCTION) continue;
        any = true;
        const int before = data_->state.errorCount;
        SpsValue delta = sps_float(dt);
        callWith(*script, script->onUpdate, &delta);
        if (data_->state.errorCount != before) script->broken = true;
    }

    updateTimers(*data_, dt);
    updateTriggers(*data_);
    updateButtons(*data_);
    return any;
#else
    (void)dt;
    return false;
#endif
}

bool Host::callFixedUpdate(float dt) {
#if SPECTRA_SPSCRIPT
    bool any = false;
    for (const std::unique_ptr<Script>& script : data_->scripts) {
        if (script->broken || !script->context) continue;
        if (script->onFixedUpdate.type != SPS_FUNCTION) continue;
        any = true;
        const int before = data_->state.errorCount;
        SpsValue delta = sps_float(dt);
        callWith(*script, script->onFixedUpdate, &delta);
        if (data_->state.errorCount != before) script->broken = true;
    }
    return any;
#else
    (void)dt;
    return false;
#endif
}

bool Host::callStop() {
#if SPECTRA_SPSCRIPT
    bool any = false;
    for (const std::unique_ptr<Script>& script : data_->scripts) {
        if (script->broken || !script->context) continue;
        if (script->onStop.type != SPS_FUNCTION) continue;
        any = true;
        const int before = data_->state.errorCount;
        callWith(*script, script->onStop, nullptr);
        if (data_->state.errorCount != before) script->broken = true;
    }
    return any;
#else
    return false;
#endif
}

bool Host::callDestroy() {
#if SPECTRA_SPSCRIPT
    bool any = false;
    for (const std::unique_ptr<Script>& script : data_->scripts) {
        if (script->broken || !script->context) continue;
        if (script->onDestroy.type != SPS_FUNCTION) continue;
        any = true;
        const int before = data_->state.errorCount;
        callWith(*script, script->onDestroy, nullptr);
        if (data_->state.errorCount != before) script->broken = true;
    }
    return any;
#else
    return false;
#endif
}

int Host::scriptCount() const { return (int)data_->scripts.size(); }

std::string Host::scriptName(int index) const {
    if (index < 0 || index >= (int)data_->scripts.size()) return std::string();
    return data_->scripts[(size_t)index]->name;
}

const std::vector<Host::Diagnostic>& Host::diagnostics() const { return data_->diagnostics; }

void Host::clearDiagnostics() { data_->diagnostics.clear(); }

const std::string& Host::engineVersion() const { return data_->version; }

bool Host::available() const { return data_->available; }

} // namespace sps
} // namespace s3d
