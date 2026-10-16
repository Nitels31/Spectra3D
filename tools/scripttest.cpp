// Console verification for the Host entry-point rule and the three optional
// hooks. Built as `scripttest`; it needs no window, no assets and no project, so
// it links the script layer plus the world it mutates and nothing else.
//
// The observables are deliberately the ones a caller would use: the log ring
// buffer a script reaches through engine::log(), the body count it changes
// through the spawning bindings, and the diagnostic sink the Host publishes. No
// internals are poked at, so a passing run says the public contract holds rather
// than that a particular private helper was reached.

#include "script/ScriptHost.hpp"

#include "assets/MeshRegistry.hpp"
#include "core/Log.hpp"
#include "physics/PhysicsWorld.hpp"
#include "render/Camera.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace s3d;
using namespace s3d::script;

namespace {

int g_checks = 0;
int g_failures = 0;

// One line per assertion so a failure names the behaviour that broke.
void check(bool condition, const char* what) {
    ++g_checks;
    if (condition) {
        std::printf("  ok   %s\n", what);
        return;
    }
    ++g_failures;
    std::printf("  FAIL %s\n", what);
}

void heading(const char* what) { std::printf("\n== %s ==\n", what); }

// Counts log lines containing `needle`. engine::log() writes through Log, which
// is the same sink the editor console panel reads, so a script saying something
// shows up here exactly once per call.
int countLines(const char* needle) {
    int found = 0;
    for (const Log::Entry& e : Log::entries()) {
        if (std::strstr(e.text, needle)) ++found;
    }
    return found;
}

// Every message the Host emits, kept so a test can assert on the diagnostic
// itself rather than only on the false return that accompanies it.
std::vector<Host::Diagnostic> g_seen;

void collect(const Host::Diagnostic& d) { g_seen.push_back(d); }

int errorCount() {
    int found = 0;
    for (const Host::Diagnostic& d : g_seen) {
        if (d.isError) ++found;
    }
    return found;
}

// The counterpart, for the one case where a warning is the whole point: two
// scripts declaring the same hook. An error would break a working project, so the
// engine reports it as a warning and resolves it first-wins.
int warningCount() {
    int found = 0;
    for (const Host::Diagnostic& d : g_seen) {
        if (!d.isError) ++found;
    }
    return found;
}

int errorCount(const std::vector<Host::Diagnostic>& list) {
    int found = 0;
    for (const Host::Diagnostic& d : list) {
        if (d.isError) ++found;
    }
    return found;
}

// A helper next to the entry point. Calling every `void()` would run makeForest()
// on its own as well, so the script would log "helper" twice and spawn two boxes;
// the single-entry-point rule has to run the pair exactly once.
const char* kHelperAndEntry =
    "void makeForest() {\n"
    "    engine::log(\"helper-line\");\n"
    "    engine::spawnBox(0.0f, 10.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0);\n"
    "}\n"
    "\n"
    "void onInit() {\n"
    "    engine::log(\"entry-line\");\n"
    "    engine::spawnBox(0.0f, 30.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0);\n"
    "    makeForest();\n"
    "}\n";

// onUpdate is a `void()` like any other, so with no other candidate it is also
// the entry point and runs once during the run. The body count therefore moves
// by one here and by one again on every callFunction() below.
const char* kUpdateHook =
    "void onUpdate(float dt) {\n"
    "    engine::log(\"tick\");\n"
    "    engine::spawnBox(0.0f, 20.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);\n"
    "}\n";

// The no-argument form, which every script written before dt existed declares.
// It has to keep working, because the alternative is that all of them stop
// updating the moment the signature changes.
const char* kLegacyUpdateHook =
    "void onUpdate() {\n"
    "    engine::log(\"legacy-tick\");\n"
    "}\n";

// The fixed-step hook. Declared alongside onUpdate so the two can be told apart
// by what they do rather than only by what they are called.
const char* kBothHooks =
    "void onUpdate(float dt) {\n"
    "    engine::log(\"frame\");\n"
    "}\n"
    "\n"
    "void onFixedUpdate(float dt) {\n"
    "    engine::log(\"fixed\");\n"
    "}\n"
    "\n"
    "void onStop() {\n"
    "    engine::log(\"stopped\");\n"
    "}\n";

// An exception raised from a hook, and a second hook behind it. The point is the
// context: a context left mid-execution cannot be reused, so the next call has to
// work. If the recovery path were wrong, the second hook would never run.
//
// assert(false, ...) and not `throw`: `throw` is not a reserved word in this
// AngelScript build - `try` is in the token table and `throw` is not - so a script
// that writes it does not compile, and `assert` is not a symbol the default
// library provides here either. Integer division by zero is the exception this
// build does raise on its own.
const char* kThrowingHook =
    "void onUpdate(float dt) {\n"
    "    engine::log(\"throwing\");\n"
    "    int a = 1;\n"
    "    int b = 0;\n"
    "    int c = a / b;\n"
    "    engine::log(\"never-reached\");\n"
    "}\n"
    "\n"
    "void onStop() {\n"
    "    engine::log(\"after-throw\");\n"
    "}\n";

// The input bindings, with no window behind them. Every one of these has to
// answer "no" or "zero" rather than dereference a null pointer - that is the whole
// reason the window is allowed to be absent.
//
// Written as if/else chains of plain literals rather than string arithmetic: the
// engine's `string` has opAdd(string) and not opAdd(int), so `"key=" + (x ? 1 : 0)`
// is a compile error and would be testing the string type instead of the input.
const char* kInputProbe =
    "void onInit() {\n"
    "    if (engine::isKeyDown(engine::KeyW)) engine::log(\"key-held\");\n"
    "    else engine::log(\"key-free\");\n"
    "    if (engine::isKeyDown(99999)) engine::log(\"range-bad\");\n"
    "    else engine::log(\"range-ok\");\n"
    "    if (engine::isKeyDown(-1)) engine::log(\"negative-bad\");\n"
    "    else engine::log(\"negative-ok\");\n"
    "    if (engine::isMouseButtonDown(0)) engine::log(\"button-held\");\n"
    "    else engine::log(\"button-free\");\n"
    "    if (engine::isMouseButtonDown(99)) engine::log(\"button-bad\");\n"
    "    else engine::log(\"button-ok\");\n"
    "    float dx = 1.0f, dy = 1.0f;\n"
    "    if (engine::getMouseDelta(dx, dy)) engine::log(\"delta-some\");\n"
    "    else engine::log(\"delta-none\");\n"
    "    if (dx == 0.0f && dy == 0.0f) engine::log(\"delta-zeroed\");\n"
    "    else engine::log(\"delta-leftovers\");\n"
    "    engine::setMouseCaptured(true);\n"
    "    if (engine::isMouseCaptured()) engine::log(\"captured-bad\");\n"
    "    else engine::log(\"captured-ok\");\n"
    "    if (engine::getScrollDelta() == 0.0) engine::log(\"scroll-zero\");\n"
    "    else engine::log(\"scroll-bad\");\n"
    "}\n";

// The same pair with the entry point spelled `OnInit`. str::equalsIgnoreAscii()
// folds only the script's side of the comparison, so an entry point that is not
// already lower case is exactly the case that used to lose to the helper.
const char* kMixedCaseEntry =
    "void makeForest() {\n"
    "    engine::log(\"helper-line\");\n"
    "    engine::spawnBox(0.0f, 10.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0);\n"
    "}\n"
    "\n"
    "void OnInit() {\n"
    "    engine::log(\"entry-line\");\n"
    "    engine::spawnBox(0.0f, 30.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0);\n"
    "    makeForest();\n"
    "}\n";

// No onInit, main or start anywhere, so the documented fallback applies: the
// first `void()` in declaration order is the entry point and the rest are helpers.
const char* kNoPreferredName =
    "void firstHelper() {\n"
    "    engine::log(\"first-line\");\n"
    "}\n"
    "\n"
    "void secondHelper() {\n"
    "    engine::log(\"second-line\");\n"
    "}\n";

// Nothing but shared helpers: there is no entry point to run, which has to be a
// success that logs nothing rather than a failure.
const char* kSharedOnly = "shared void unusedHelper() {\n    engine::log(\"never\");\n}\n";

// A declaration that cannot parse. The message callback must report it as an
// error and runString() must refuse.
const char* kBroken = "void onInit( {\n    engine::log(\"nope\");\n}\n";

} // namespace

int main() {
    // Log::setFileDirectory() is left alone on purpose: stderr is unbuffered and
    // always written, so the diagnostic lines below are visible without asking
    // for a log file next to the executable.
    Log::clear();

    PhysicsWorld world;
    MeshRegistry meshes;
    Camera camera;
    // No window, on purpose: the whole point of this harness is that it runs
    // headless. It is also a test in itself - every input binding has to answer
    // "no" rather than dereference a null window.
    Host host(world, meshes, camera, nullptr);
    host.setDiagnosticSink(collect);

    heading("initialize");
    const bool started = host.initialize();
    check(started, "initialize() succeeds");
    check(host.available(), "available() is true after initialize()");
    check(!host.engineVersion().empty(), "engineVersion() is reported");

    // --- lookup covers bindings as well as script globals -------------------
    heading("lookup");
    check(host.hasFunction("void clearScene()"), "hasFunction() resolves a registered binding");
    check(!host.hasFunction("void noSuchThing()"), "hasFunction() rejects an unknown declaration");
    check(!host.hasFunction(nullptr), "hasFunction() tolerates a null declaration");

    // --- (a) one entry point per script ------------------------------------
    heading("single entry point");
    g_seen.clear();
    Log::clear();
    const int bodiesBefore = (int)world.bodies.size();
    const bool ranEntry = host.runString("helper_and_entry.as", kHelperAndEntry);
    check(ranEntry, "runString() succeeds");
    check(countLines("entry-line") == 1, "the onInit entry point ran");
    check(countLines("helper-line") == 1, "the helper ran only as a callee");
    check((int)world.bodies.size() == bodiesBefore + 2,
          "onInit plus its helper spawned two bodies");
    check(errorCount() == 0, "no error diagnostic");

    // --- the entry point wins whatever its case ---------------------------
    heading("entry point case");
    g_seen.clear();
    Log::clear();
    const int bodiesBeforeMixed = (int)world.bodies.size();
    check(host.runString("mixed_case_entry.as", kMixedCaseEntry),
          "runString() succeeds for OnInit");
    check(countLines("entry-line") == 1, "a mixed case OnInit still beat the helper");
    check(countLines("helper-line") == 1, "and its helper ran once as a callee");
    check((int)world.bodies.size() == bodiesBeforeMixed + 2,
          "OnInit plus its helper spawned two bodies");

    // --- no preferred name falls back to the first void() -------------------
    heading("no preferred name");
    g_seen.clear();
    Log::clear();
    const bool ranFallback = host.runString("no_preferred_name.as", kNoPreferredName);
    check(ranFallback, "a script with no preferred name still succeeds");
    check(countLines("first-line") == 1, "the first void() ran as the entry point");
    check(countLines("second-line") == 0, "the second void() stayed a helper");

    // --- (b) an absent hook is not a failure -------------------------------
    heading("absent hook");
    check(!host.hasFunction(Host::kOnUpdateDeclaration), "hasFunction() sees no onUpdate yet");
    check(!host.hasFunction(Host::kOnFixedUpdateDeclaration),
          "hasFunction() sees no onFixedUpdate yet");
    Log::clear();
    const bool calledMissing = host.callFunction(Host::kOnUpdateDeclaration);
    check(!calledMissing, "callFunction() on an absent hook returns false");
    check(!host.callUpdate(0.016f), "callUpdate() on a script with no hook returns false");
    check(!host.callFixedUpdate(0.016f), "callFixedUpdate() does the same");
    check(Log::entries().empty(), "callFunction() on an absent hook logs nothing");
    // Asked twice on purpose: the second answer comes from the cache, and a cache
    // that answers differently from the search is worse than no cache.
    check(!host.callUpdate(0.016f), "a second callUpdate() still returns false");
    check(Log::entries().empty(), "and still logs nothing");

    // --- (c) a declared hook runs, every call ------------------------------
    //
    // onUpdate(float) is NOT an entry point: isEntryPoint() requires no
    // parameters, and a per-frame hook that also fired on load would be a
    // surprise. So this script runs nothing when it is built, and that is asserted
    // rather than assumed.
    heading("declared hook");
    g_seen.clear();
    Log::clear();
    const int bodiesBeforeHook = (int)world.bodies.size();
    check(host.runString("hook.as", kUpdateHook), "runString() succeeds for onUpdate");
    check(host.hasFunction(Host::kOnUpdateDeclaration),
          "hasFunction() sees the declared onUpdate");
    check(!host.hasFunction(Host::kOnUpdateLegacyDeclaration),
          "the no-argument form is NOT visible when a dt form is declared");
    check(countLines("tick") == 0, "onUpdate(float) is not an entry point, so it ran nothing");
    check((int)world.bodies.size() == bodiesBeforeHook, "and spawned nothing on load");

    Log::clear();
    const bool calledOnce = host.callUpdate(0.016f);
    check(calledOnce, "callUpdate() on a declared hook returns true");
    check(countLines("tick") == 1, "the hook ran, so it logged once");
    check((int)world.bodies.size() == bodiesBeforeHook + 1, "and spawned one body");

    Log::clear();
    const bool calledAgain = host.callUpdate(0.033f);
    check(calledAgain, "a second callUpdate() also returns true");
    check(countLines("tick") == 1, "and runs again");
    check((int)world.bodies.size() == bodiesBeforeHook + 2, "and spawned a second body");

    Log::clear();
    const bool calledStop = host.callStop();
    check(!calledStop, "onStop was never declared, so callStop() returns false");
    check(Log::entries().empty(), "and an absent onStop logs nothing");

    // --- the two per-frame hooks are separate functions --------------------
    // A script that moves a body from onUpdate and one that moves it from
    // onFixedUpdate look identical in the source and behave differently on a
    // machine with a different frame rate. That difference is the whole reason the
    // two exist, so they are counted separately here.
    //
    // Same module name as the previous script, so it replaces it - see the note on
    // the legacy section below for why that matters.
    heading("update vs fixed update");
    g_seen.clear();
    Log::clear();
    check(host.runString("hook.as", kBothHooks), "a script with both hooks compiles");
    Log::clear();
    check(host.callUpdate(0.016f), "callUpdate() finds the frame hook");
    check(countLines("frame") == 1, "and runs only that one");
    check(countLines("fixed") == 0, "the fixed hook did not run with it");
    Log::clear();
    check(host.callFixedUpdate(0.016f), "callFixedUpdate() finds the fixed hook");
    check(countLines("fixed") == 1, "and runs only that one");
    check(countLines("frame") == 0, "the frame hook did not run with it");
    Log::clear();
    check(host.callStop(), "callStop() finds onStop");
    check(countLines("stopped") == 1, "and runs it");

    // Every hook script below is loaded under the SAME module name, so each run
    // replaces the one before it and at most one module ever declares a hook.
    // resolveFunction() sweeps modules in creation order and takes the first
    // match, which is right for the engine and terrible for a test: a leftover
    // script from three sections ago keeps answering and every count here lies.

    // --- the no-argument onUpdate still works ------------------------------
    heading("legacy onUpdate");
    g_seen.clear();
    Log::clear();
    check(host.runString("hook.as", kLegacyUpdateHook),
          "a script with the no-argument onUpdate compiles");
    check(!host.hasFunction(Host::kOnUpdateDeclaration),
          "the dt form is absent, so only the legacy one is found");
    Log::clear();
    check(host.callUpdate(0.016f), "callUpdate() falls back to the no-argument form");
    check(countLines("legacy-tick") == 1, "and runs it");

    // --- a hook that throws must not poison the next one -------------------
    // The context is kept now, and a context left mid-execution cannot be reused.
    // Unprepare() is the supported way out of that, and this is the test for it: a
    // recovery that does not work shows up as a second hook that never runs.
    //
    // AngelScript has no throw statement, so the script raises the one exception a
    // script can raise on its own: integer division by zero. The VM turns that into
    // asBC_DIV's internal exception rather than producing 0, which is what makes it
    // usable here.
    heading("hook that throws");
    g_seen.clear();
    Log::clear();
    check(host.runString("hook.as", kThrowingHook), "a throwing hook compiles");
    Log::clear();
    const bool threwOk = host.callUpdate(0.016f);
    check(!threwOk, "a hook that throws reports failure");
    check(countLines("throwing") == 1, "and ran far enough to throw");
    check(errorCount() > 0, "the throw produced an error diagnostic");
    Log::clear();
    const bool afterThrow = host.callStop();
    check(afterThrow, "a different hook still runs after the throw");
    check(countLines("after-throw") == 1, "so the context was recovered, not abandoned");

    // --- the cache cannot outlive the module it points into -----------------
    // asGM_ALWAYS_CREATE replaces a module on every run, so a cached pointer into
    // the old one is dangling. A MISS is just as wrong: a script that declared
    // onUpdate a moment ago can declare it now.
    heading("function cache");
    g_seen.clear();
    Log::clear();
    check(host.runString("hook.as", kSharedOnly), "a script with no hook compiles");
    check(!host.callUpdate(0.016f), "callUpdate() caches the miss");
    check(host.runString("hook.as", kUpdateHook), "a later script declares onUpdate");
    check(host.hasFunction(Host::kOnUpdateDeclaration),
          "the cached miss did not survive the new module");
    check(host.callUpdate(0.016f), "and the hook is callable the moment it exists");

    // --- two scripts, one hook: reported, not silently swallowed ------------
    // Last, because it is the one case that deliberately leaves two modules
    // declaring the same hook, and everything after it would inherit that.
    heading("hook declared twice");
    g_seen.clear();
    Log::clear();
    check(host.runString("dup_a.as", kUpdateHook), "a second script declares onUpdate");
    check(host.runString("dup_b.as", kUpdateHook), "and a third one does too");
    check(errorCount() == 0, "loading them raises no error");
    Log::clear();
    check(host.callUpdate(0.016f), "the hook is still callable");
    check(countLines("tick") == 1, "exactly one script is called, not three");
    // Checked after the call, not before: the lookup is lazy, so the warning is
    // emitted while callUpdate() resolves the name, and asking any earlier would
    // be testing a question nobody has asked yet.
    check(warningCount() > 0, "and the duplication is reported");

    // --- input bindings with no window --------------------------------------
    heading("input without a window");
    g_seen.clear();
    Log::clear();
    check(host.runString("input_probe.as", kInputProbe), "the input probe compiles");
    check(countLines("key-free") == 1, "isKeyDown() answers false with no window");
    check(countLines("range-ok") == 1, "and false for a key out of range");
    check(countLines("negative-ok") == 1, "and false for a negative key");
    check(countLines("button-free") == 1, "isMouseButtonDown() answers false");
    check(countLines("button-ok") == 1, "and false for a button out of range");
    check(countLines("delta-none") == 1, "getMouseDelta() reports no movement");
    check(countLines("delta-zeroed") == 1, "and zeroes the outs rather than leaving them");
    check(countLines("captured-ok") == 1, "setMouseCaptured() is inert, isMouseCaptured() false");
    check(countLines("scroll-zero") == 1, "getScrollDelta() answers zero");
    check(errorCount() == 0, "none of the input bindings logged an error");

    // --- (d) a compile error is reported as an error -----------------------
    heading("compile error");
    g_seen.clear();
    Log::clear();
    const bool ranBroken = host.runString("broken.as", kBroken);
    check(!ranBroken, "runString() returns false on a compile error");
    check(errorCount() > 0, "an error diagnostic was published to the sink");
    check(errorCount(host.diagnostics()) == errorCount(),
          "diagnostics() retains the same errors for the editor panel");

    // --- a script with only shared helpers ---------------------------------
    heading("helpers only");
    g_seen.clear();
    Log::clear();
    const bool ranShared = host.runString("shared_only.as", kSharedOnly);
    check(ranShared, "a script with no entry point still succeeds");
    check(countLines("never") == 0, "and runs nothing");
    check(Log::entries().empty(), "and logs nothing at all");

    // --- runFile shares the rule -------------------------------------------
    heading("runFile");
    g_seen.clear();
    Log::clear();
    const std::string path = "scripttest_entry.as";
    {
        std::ofstream out(path);
        out << kHelperAndEntry;
    }
    const bool ranFile = host.runFile(path);
    check(ranFile, "runFile() succeeds");
    check(countLines("entry-line") == 1, "onInit logged exactly once from a file");
    check(countLines("helper-line") == 1, "the helper logged exactly once from a file");
    check(!host.runFile("scripttest_missing.as"), "runFile() on a missing file returns false");
    std::remove(path.c_str());

    heading("shutdown");
    host.shutdown();
    check(!host.available(), "available() is false after shutdown()");
    check(!host.hasFunction(Host::kOnUpdateDeclaration), "hasFunction() is false after shutdown()");
    check(!host.callUpdate(0.016f), "callUpdate() is false after shutdown()");
    check(!host.callFixedUpdate(0.016f), "callFixedUpdate() is false after shutdown()");
    check(!host.callFunction(Host::kOnUpdateDeclaration),
          "callFunction() is false after shutdown()");

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}