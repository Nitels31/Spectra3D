#include "script/ScriptHost.hpp"

#include "script/Bindings.hpp"
#include "core/Log.hpp"
#include "core/Str.hpp"

#if SPECTRA_SCRIPTING
#include <angelscript.h>
#endif

#include <fstream>
#include <utility>

namespace s3d {
namespace script {
namespace {

// Severity of one message. AngelScript reports three levels and the host reuses
// the same enum for its own reports (file missing, engine unavailable) so both
// kinds funnel through emit().
enum class MsgLevel { Information, Warning, Error };

// Where a message goes while a run is in flight. Published rather than reached
// through the Host because the engine's message callback is a bare function
// pointer with no receiver, and the engine's global functions likewise.
struct MessageSink {
    const Host* owner = nullptr;
    Host::DiagnosticSink callback;
    std::vector<Host::Diagnostic>* list = nullptr;
};

MessageSink* g_sink = nullptr;
ScriptState* g_state = nullptr;

// One message, three destinations: the caller's sink, the run's diagnostics list
// and the engine log. A sink that belongs to another Host is skipped rather than
// written to, so a refused second Host still explains itself through the log
// without corrupting the live one's diagnostics.
void emit(const Host* owner, MsgLevel level, const char* section, int row, int col,
          const char* text) {
    const char* message = text ? text : "";

    Host::Diagnostic d;
    d.section = section ? section : "";
    d.row = row;
    d.col = col;
    d.message = message;
    d.isError = level == MsgLevel::Error;

    MessageSink* sink = (g_sink && g_sink->owner == owner && g_sink->list) ? g_sink : nullptr;
    // AngelScript's information messages are compile chatter ("Compiling script
    // section ..."), so only warnings and errors are worth keeping around.
    if (sink) {
        if (level != MsgLevel::Information) sink->list->push_back(d);
        if (sink->callback) sink->callback(d);
    }

    const char* where = d.section.empty() ? "script" : d.section.c_str();
    switch (level) {
        case MsgLevel::Error:
            Log::error("%s(%d,%d): %s", where, d.row, d.col, message);
            break;
        case MsgLevel::Warning:
            Log::warn("%s(%d,%d): %s", where, d.row, d.col, message);
            break;
        case MsgLevel::Information:
            break;
    }
}

#if SPECTRA_SCRIPTING
void compilerMessage(const asSMessageInfo* msg, void* obj) {
    if (!msg) return;
    MsgLevel level = MsgLevel::Information;
    if (msg->type == asMSGTYPE_ERROR)
        level = MsgLevel::Error;
    else if (msg->type == asMSGTYPE_WARNING)
        level = MsgLevel::Warning;
    emit(static_cast<const Host*>(obj), level, msg->section, msg->row, msg->col, msg->message);
}

// A runnable entry point: a global function that returns nothing, takes nothing
// and is not marked `shared`. Everything else is a helper the author is expected
// to call from one of these.
bool isEntryPoint(const asIScriptFunction* fn) {
    return fn && !fn->IsShared() && fn->GetParamCount() == 0 &&
           fn->GetReturnTypeId() == asTYPEID_VOID;
}

#endif

// Module names are only labels to anything downstream, but keeping them to word
// characters makes them usable as identifiers if a diagnostic ever quotes one.
std::string moduleNameFor(const std::string& scriptName) {
    const std::string stem = str::fileStem(scriptName.empty() ? std::string("script") : scriptName);

    std::string out = "script_";
    out.reserve(stem.size() + 16);
    for (char c : stem) {
        const bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '_';
        out += word ? c : '_';
    }
    if (out.size() == 7) out += "unnamed";
    return out;
}

#if SPECTRA_SCRIPTING
bool readWholeFile(const std::string& path, std::string& text) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    file.seekg(0, std::ios::end);
    const std::streamoff end = file.tellg();
    if (end <= 0) {
        text.clear();
        return true;
    }
    file.seekg(0, std::ios::beg);
    text.resize((size_t)end);
    file.read(&text[0], end);
    text.resize((size_t)file.gcount());
    return true;
}
#endif

} // namespace

#if SPECTRA_SCRIPTING

// Explains a context that did not reach asEXECUTION_FINISHED. The innermost
// stack frame carries the useful line; no function object is touched, so there
// is no refcount to get wrong.
//
// A member, not a free function: it reads and writes lastErrorKey_ to deduplicate,
// and a free function would need a const_cast or a public accessor to do that.
// Defined here rather than in the anonymous namespace above because a member
// definition has to sit in the namespace that encloses the class, not in a child
// of it.
void Host::emitExecutionFailure(void* ctx, const char* moduleName, const char* functionName) {
    asIScriptContext* context = static_cast<asIScriptContext*>(ctx);
    if (context->GetState() != asEXECUTION_EXCEPTION) {
        emit(this, MsgLevel::Error, moduleName, 0, 0, "execution did not finish");
        return;
    }

    int col = 0;
    const char* section = nullptr;
    const int row = context->GetLineNumber(0, &col, &section);
    const char* reason = context->GetExceptionString();

    const std::string text = std::string("exception in ") + (functionName ? functionName : "?") +
                             ": " + (reason ? reason : "unknown");
    // Deduplicated by identity: module, line, column and message. A hook that
    // throws every frame is the common case - a script with a bug in onUpdate -
    // and at 144 Hz that is 144 identical lines a second into a ring buffer that
    // is sized to hold the interesting diagnostics. The first failure is always
    // logged; only the repetition is suppressed.
    const std::string key = std::string(moduleName ? moduleName : "?") + ":" +
                            std::to_string(row) + ":" + std::to_string(col) + ":" + text;
    if (key == lastErrorKey_) return;
    lastErrorKey_ = key;
    emit(this, MsgLevel::Error, section ? section : moduleName, row, col, text.c_str());
}

#endif

// --- shared binding state ---------------------------------------------------

ScriptState* activeState() { return g_state; }

#if SPECTRA_SCRIPTING
int registerBindings(asIScriptEngine* engine, const Binding* bindings, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const int r =
            engine->RegisterGlobalFunction(bindings[i].declaration, bindings[i].function,
                                           asCALL_CDECL);
        if (r < 0) return asNO_MODULE;
    }
    return 0;
}
#endif

RigidBody* resolveBody(const char* fn, int index) {
    ScriptState* state = activeState();
    if (!state || !state->world) {
        Log::error("engine::%s: no script host is initialised", fn);
        return nullptr;
    }

    RigidBody* body = state->world->find(index);
    if (!body)
        Log::error("engine::%s: body %d is out of range (%d bodies)", fn, index,
                   (int)state->world->bodies.size());
    return body;
}

// --- Host -------------------------------------------------------------------

Host::Host(PhysicsWorld& world, MeshRegistry& meshes, Camera& camera, Window* window,
           SoundDevice* sound)
    : world_(world), meshes_(meshes), camera_(camera), window_(window), sound_(sound) {}

Host::~Host() { shutdown(); }

bool Host::initialize() {
#if SPECTRA_SCRIPTING
    if (engine_) return true;

    // AngelScript global functions have no receiver, so exactly one Host may own
    // the engine at a time. Refusing loudly beats silently sharing state.
    if (g_state) {
        emit(this, MsgLevel::Error, "", 0, 0, "another script host already owns the engine");
        return false;
    }

    static ScriptState state;
    static MessageSink sink;
    // Fresh session: groups hold body indices from the world as it was when they
    // were recorded, and a new engine must not inherit them.
    state.groups.clear();
    state.groupNames.clear();
    state.world = &world_;
    state.meshes = &meshes_;
    state.camera = &camera_;
    state.window = window_;
    state.sound = sound_;
    sink.owner = this;
    sink.callback = sink_;
    sink.list = &diagnostics_;

    asIScriptEngine* engine = asCreateScriptEngine();
    if (!engine) {
        emit(this, MsgLevel::Error, "", 0, 0, "asCreateScriptEngine() returned null");
        return false;
    }
    const char* libraryVersion = asGetLibraryVersion();
    version_ = libraryVersion ? libraryVersion : "";

    engine_ = engine;

    engine->SetMessageCallback(asFUNCTION(compilerMessage), const_cast<Host*>(this), asCALL_CDECL);

    // `string` has to exist before any declaration can mention one, and it has to
    // be registered while the default namespace is still the global one.
    if (registerStringType(engine) < 0) {
        emit(this, MsgLevel::Error, "", 0, 0, "script string type could not be registered");
        shutdown();
        return false;
    }

    // One call puts every binding below into `engine`. 2.38's declaration parser
    // rejects a `namespace` modifier on RegisterGlobalFunction, and qualified
    // names ("engine::void f(...)") only work once the namespace exists and are
    // not accepted by RegisterEnum at all - the default namespace is the only
    // route that works for functions and enums alike.
    if (engine->SetDefaultNamespace("engine") < 0) {
        emit(this, MsgLevel::Error, "", 0, 0, "script namespace 'engine' could not be created");
        shutdown();
        return false;
    }

    if (registerEngineBindings(engine) < 0 || registerSceneBindings(engine) < 0 ||
        registerCameraBindings(engine) < 0 || registerInputBindings(engine) < 0 ||
        registerSoundBindings(engine) < 0) {
        emit(this, MsgLevel::Error, "", 0, 0, "binding registration failed; engine disabled");
        shutdown();
        return false;
    }

    // Published only once the bindings exist, so no binding can observe a
    // half-built host.
    g_sink = &sink;
    g_state = &state;

    Log::info("Scripting engine %s ready", version_.c_str());
    return true;
#else
    emit(this, MsgLevel::Error, "", 0, 0, "built without scripting (SPECTRA_SCRIPTING=OFF)");
    return false;
#endif
}

void Host::shutdown() {
#if SPECTRA_SCRIPTING
    if (!engine_) return;
    // The context is released before the engine that owns it. Skipping this is
    // the one shutdown bug that would now be new: a kept context has a
    // destructor where the per-call one had a Release() on every path.
    if (context_) {
        static_cast<asIScriptContext*>(context_)->Release();
        context_ = nullptr;
    }
    clearFunctionCache();
    static_cast<asIScriptEngine*>(engine_)->ShutDownAndRelease();
#endif
    engine_ = nullptr;
    version_.clear();
    g_state = nullptr;
    g_sink = nullptr;
}

void Host::setDiagnosticSink(DiagnosticSink sink) {
    sink_ = std::move(sink);
    if (g_sink && g_sink->owner == this) g_sink->callback = std::move(sink_);
}

bool Host::runString(const std::string& scriptName, const std::string& code) {
    if (!available()) return false;

    clearDiagnostics();

    const std::string section = scriptName.empty() ? std::string("script") : scriptName;
    return buildAndRun(moduleNameFor(scriptName), section, code);
}

bool Host::runFile(const std::string& path) {
    if (!available()) return false;

#if SPECTRA_SCRIPTING
    clearDiagnostics();

    std::string code;
    if (!readWholeFile(path, code)) {
        emit(this, MsgLevel::Error, path.c_str(), 0, 0, "script file could not be read");
        return false;
    }
    return runString(path, code);
#else
    return false;
#endif
}

bool Host::buildAndRun(const std::string& moduleName, const std::string& sectionName,
                       const std::string& code) {
#if SPECTRA_SCRIPTING
    asIScriptEngine* engine = static_cast<asIScriptEngine*>(engine_);
    if (!engine) return false;

    // Build() is atomic, so a module per script is what keeps one file's compile
    // error from wiping the functions of another. ALWAYS_CREATE makes a re-run
    // replace exactly this script's module and nothing else.
    //
    // That replacement is also why the resolved-declaration cache is dropped
    // here and not anywhere else: asGM_ALWAYS_CREATE discards the old module and
    // its functions, so every cached pointer that could have belonged to it is
    // now dangling, and a cached MISS is just as wrong as a stale hit - a script
    // that declared onUpdate thirty seconds ago can declare it now.
    clearFunctionCache();

    asIScriptModule* module = engine->GetModule(moduleName.c_str(), asGM_ALWAYS_CREATE);
    if (!module) {
        emit(this, MsgLevel::Error, sectionName.c_str(), 0, 0, "script module could not be created");
        return false;
    }
    if (module->AddScriptSection(sectionName.c_str(), code.c_str(), code.size(), 0) < 0) {
        emit(this, MsgLevel::Error, sectionName.c_str(), 0, 0, "script section was rejected");
        return false;
    }
    // The message callback has already explained any compile failure.
    if (module->Build() < 0) return false;

    // Exactly ONE entry point runs per script.
    //
    // Calling every `void()` function looked convenient but is a trap: a helper
    // such as `makeForest()` gets executed as well, so a script with a helper
    // and an `onInit` built its scene twice. The rule is now explicit and
    // predictable:
    //   1. a function named onInit / main / start wins;
    //   2. otherwise the first `void()` function declared in the file.
    const asUINT functionCount = module->GetFunctionCount();
    asIScriptFunction* entry = nullptr;
    asIScriptFunction* firstEntry = nullptr;
    for (asUINT i = 0; i < functionCount; ++i) {
        asIScriptFunction* candidate = module->GetFunctionByIndex(i);
        if (!isEntryPoint(candidate)) continue;
        if (!firstEntry) firstEntry = candidate;
        const char* name = candidate->GetName();
        const bool preferred = str::equalsIgnoreAscii(name, "onInit") ||
                               str::equalsIgnoreAscii(name, "main") ||
                               str::equalsIgnoreAscii(name, "start");
        if (preferred) {
            entry = candidate;
            break;
        }
    }
    if (!entry) entry = firstEntry;
    if (!entry) return true; // a script that only declares shared helpers

    return executeFunction(entry, sectionName.c_str(), 0.0f);
#else
    (void)moduleName;
    (void)sectionName;
    (void)code;
    return false;
#endif
}

void* Host::context() {
#if SPECTRA_SCRIPTING
    if (context_) return context_;
    asIScriptEngine* engine = static_cast<asIScriptEngine*>(engine_);
    if (!engine) return nullptr;
    context_ = engine->CreateContext();
    return context_;
#else
    return nullptr;
#endif
}

void Host::clearFunctionCache() { functionCache_.clear(); }

bool Host::executeFunction(void* function, const char* label, float argument) {
#if SPECTRA_SCRIPTING
    asIScriptEngine* engine = static_cast<asIScriptEngine*>(engine_);
    asIScriptFunction* fn = static_cast<asIScriptFunction*>(function);
    if (!engine || !fn) return false;

    // One context for the life of the host, not one per call.
    //
    // The reason the old code made a context per call was right - an exception
    // leaves a context mid-execution and a per-frame hook must not inherit the one
    // that raised it - but AngelScript already has the answer to that, and it is
    // Unprepare(): asCContext::Unprepare refuses only while a function is ACTIVE
    // or SUSPENDED, and for any other status except a clean FINISH it calls
    // CleanStack() before returning. Calling it after a failed Execute is the
    // supported recovery, and what it leaves behind is a context that is ready
    // for the next Prepare.
    //
    // So the same call has to be made on both paths, and the difference between
    // the paths is the report, not the cleanup. A float argument is a primitive,
    // so it never sets the context's needToCleanupArgs flag - which is the other
    // condition Unprepare asserts on.
    asIScriptContext* ctx = static_cast<asIScriptContext*>(context());
    if (!ctx) {
        emit(this, MsgLevel::Error, label, 0, 0, "no script context available");
        return false;
    }

    // The function itself belongs to its module - do not release.
    const char* name = fn->GetName();
    bool ok = true;
    if (ctx->Prepare(fn) < 0) {
        emit(this, MsgLevel::Error, label, 0, 0, "script function could not be prepared");
        ok = false;
    } else {
        // Only the per-frame hooks take an argument. An entry point is a `void()`
        // with no parameters, and SetArgFloat(0) on one of those is an error - which
        // is how the first version of this made every single script fail to run.
        if (fn->GetParamCount() > 0 && ctx->SetArgFloat(0, argument) < 0) ok = false;
        if (ok && ctx->Execute() != asEXECUTION_FINISHED) {
            emitExecutionFailure(ctx, label, name);
            ok = false;
        } else if (ok) {
            // A clean run clears the dedup key: the next failure is a new one, not
            // the same one repeated, and deserves its own line.
            lastErrorKey_.clear();
        }
        // Unconditional, and before the report for a thrown exception has any way
        // to read the context again: this is what makes the context reusable and
        // what stops one bad frame from poisoning every frame after it.
        ctx->Unprepare();
    }
    return ok;
#else
    (void)function;
    (void)label;
    (void)argument;
    return false;
#endif
}

bool Host::callFunction(const char* declaration, float argument) {
#if SPECTRA_SCRIPTING
    // A hook nobody declared is the normal case for onUpdate/onStop, so it
    // returns quietly - nothing is logged and no diagnostic is recorded, which
    // is what makes a per-frame call safe.
    void* fn = resolveFunction(declaration);
    if (!fn) return false;
    return executeFunction(fn, declaration, argument);
#else
    (void)declaration;
    (void)argument;
    return false;
#endif
}

bool Host::callUpdate(float dt) {
#if SPECTRA_SCRIPTING
    // The dt form first. A script written before it existed has `void onUpdate()`
    // and gets it rather than nothing, which is the difference between "the
    // script stopped updating" and "the script has no dt".
    if (resolveFunction(kOnUpdateDeclaration))
        return callFunction(kOnUpdateDeclaration, dt);
    if (resolveFunction(kOnUpdateLegacyDeclaration))
        return callFunction(kOnUpdateLegacyDeclaration, 0.0f);
    return false;
#else
    (void)dt;
    return false;
#endif
}

bool Host::callFixedUpdate(float dt) {
#if SPECTRA_SCRIPTING
    return callFunction(kOnFixedUpdateDeclaration, dt);
#else
    (void)dt;
    return false;
#endif
}

bool Host::callStop() {
#if SPECTRA_SCRIPTING
    return callFunction(kOnStopDeclaration);
#else
    return false;
#endif
}

bool Host::hasFunction(const char* declaration) { return resolveFunction(declaration) != nullptr; }

void* Host::resolveFunction(const char* declaration) {
#if SPECTRA_SCRIPTING
    asIScriptEngine* engine = static_cast<asIScriptEngine*>(engine_);
    if (!engine || !declaration) return nullptr;

    // The cache first, misses included. A per-frame call that has to parse a
    // declaration string and sweep every module on every frame is a per-frame
    // tax on scripts that declare nothing; a script that declares the hook pays
    // one lookup and then a pointer.
    for (const Resolved& entry : functionCache_)
        if (entry.declaration == declaration) return entry.function;

    // First the registered `engine::` bindings, then the script globals. Both
    // lookups are needed: GetGlobalFunctionByDecl only ever reads the engine's
    // own registered table, so without the module sweep it can never see an
    // `onUpdate` that a script declared - which is the hook callers want.
    void* found = nullptr;
    int owners = 0;
    if (asIScriptFunction* fn = engine->GetGlobalFunctionByDecl(declaration)) {
        found = fn;
    } else {
        const asUINT moduleCount = engine->GetModuleCount();
        for (asUINT i = 0; i < moduleCount; ++i) {
            asIScriptModule* module = engine->GetModuleByIndex(i);
            if (!module) continue;
            if (asIScriptFunction* fn = module->GetFunctionByDecl(declaration)) {
                if (!found) found = fn;
                ++owners;
            }
        }
    }

    // Two scripts declaring the same hook means one of them silently never runs,
    // and which one is decided by module creation order - which is the order the
    // files happened to be listed in. That is not something to leave to chance
    // now that the hooks are called every frame: say so, once per generation,
    // instead of shipping a bug that reads as a script that forgot to update.
    if (owners > 1)
        emit(this, MsgLevel::Warning, declaration, 0, 0,
             "declared by more than one script; only the first is called");

    functionCache_.push_back(Resolved{declaration, found});
    return found;
#else
    (void)declaration;
    return nullptr;
#endif
}

const std::vector<Host::Diagnostic>& Host::diagnostics() const { return diagnostics_; }

void Host::clearDiagnostics() { diagnostics_.clear(); }

const std::string& Host::engineVersion() const { return version_; }

bool Host::available() const { return engine_ != nullptr; }

} // namespace script
} // namespace s3d