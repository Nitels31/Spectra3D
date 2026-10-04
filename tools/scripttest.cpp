// Console verification for the Host entry-point rule and the three optional
// hooks. Built as `scripttest`; it needs no window, no assets and no project, so
// it links the script layer plus the world it mutates and nothing else.
//
// The observables are deliberately the ones a caller would use: the log ring
// buffer a script reaches through engine::log(), the body count it changes
// through the spawning bindings, and the diagnostic sink the Host publishes. No
// internals are poked at, so a passing run says the public contract holds rather
// than that a particular private helper was reached.

#include "../src/script/ScriptHost.hpp"

#include "../src/assets/MeshRegistry.hpp"
#include "../src/core/Log.hpp"
#include "../src/physics/PhysicsWorld.hpp"
#include "../src/render/Camera.hpp"

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
    "void onUpdate() {\n"
    "    engine::log(\"tick\");\n"
    "    engine::spawnBox(0.0f, 20.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0);\n"
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
    Host host(world, meshes, camera);
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
    Log::clear();
    const bool calledMissing = host.callFunction(Host::kOnUpdateDeclaration);
    check(!calledMissing, "callFunction() on an absent hook returns false");
    check(Log::entries().empty(), "callFunction() on an absent hook logs nothing");

    // --- (c) a declared hook runs, every call ------------------------------
    heading("declared hook");
    g_seen.clear();
    Log::clear();
    const int bodiesBeforeHook = (int)world.bodies.size();
    check(host.runString("update_hook.as", kUpdateHook), "runString() succeeds for onUpdate");
    check(host.hasFunction(Host::kOnUpdateDeclaration), "hasFunction() sees the declared onUpdate");
    check(countLines("tick") == 1, "onUpdate ran once as the entry point");
    check((int)world.bodies.size() == bodiesBeforeHook + 1, "and spawned one body");

    Log::clear();
    const bool calledOnce = host.callFunction(Host::kOnUpdateDeclaration);
    check(calledOnce, "callFunction() on a declared hook returns true");
    check(countLines("tick") == 1, "the hook ran, so it logged once");
    check((int)world.bodies.size() == bodiesBeforeHook + 2, "and spawned a second body");

    Log::clear();
    const bool calledAgain = host.callFunction(Host::kOnUpdateDeclaration);
    check(calledAgain, "a second callFunction() also returns true");
    check(countLines("tick") == 1, "and runs again");
    check((int)world.bodies.size() == bodiesBeforeHook + 3, "and spawned a third body");

    Log::clear();
    const bool calledStop = host.callFunction(Host::kOnStopDeclaration);
    check(!calledStop, "onStop was never declared, so callFunction() returns false");
    check(Log::entries().empty(), "and an absent onStop logs nothing");

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
    check(!host.callFunction(Host::kOnUpdateDeclaration),
          "callFunction() is false after shutdown()");

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}