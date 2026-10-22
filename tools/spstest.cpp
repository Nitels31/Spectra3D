// Console verification for the Spectra3D Script host: the classes of the
// engine, the hooks, the events, the timers, the data files and the reload.
//
// Built as `spstest`; it needs no window, no assets and no project, so it
// links the script layer plus the world it mutates and nothing else.
//
// The observables are the ones a caller would use: the log ring buffer a
// script reaches through log(), the bodies it changes through the classes,
// and the diagnostic sink the host publishes. No internals are poked at, so a
// passing run says the public contract holds rather than that a particular
// private helper was reached.

#include "script/SpsHost.hpp"

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

// Counts log lines containing `needle`. log() writes through Log, which is the
// same sink the editor console panel reads, so a script saying something shows
// up here exactly once per call.
int countLines(const char* needle) {
    int found = 0;
    for (const Log::Entry& e : Log::entries()) {
        if (std::strstr(e.text, needle)) ++found;
    }
    return found;
}

// Every message the host emits, kept so a test can assert on the diagnostic
// itself rather than only on the false return that accompanies it.
std::vector<sps::Host::Diagnostic> g_seen;

void collect(const sps::Host::Diagnostic& d) { g_seen.push_back(d); }

int errorCount() {
    int found = 0;
    for (const sps::Host::Diagnostic& d : g_seen) {
        if (d.isError) ++found;
    }
    return found;
}

// A script with every hook of the language: the entry point, the two per-frame
// hooks and the one that runs when the play mode ends.
const char* kFullScript =
    "var player = Entity(\"player\")\n"
    "player.position = vec3(1, 2, 3)\n"
    "player.size = vec3(2, 2, 2)\n"
    "player.tag = \"hero\"\n"
    "\n"
    "def on_start() {\n"
    "    log(\"started\", player.name, player.tag)\n"
    "}\n"
    "\n"
    "def on_update(delta) {\n"
    "    log(\"tick\", delta)\n"
    "}\n"
    "\n"
    "def on_fixed_update(delta) {\n"
    "    log(\"step\", delta)\n"
    "}\n"
    "\n"
    "def on_stop() {\n"
    "    log(\"stopped\")\n"
    "}\n";

// A script with no hooks at all: it is a valid script and it costs nothing.
const char* kHelpersOnly = "def helper() { log(\"helper\") }\n";

// A script that fails to compile: a name the runtime does not know.
const char* kBroken = "var thing = noSuchThing\n";

// A script that runs and then fails at run time.
const char* kRuntimeFailure =
    "def on_update(delta) {\n"
    "    var nothing = nil\n"
    "    log(nothing.missing)\n"
    "}\n";

// Two scripts that share the world but not their own variables.
const char* kFirst = "var mine = Entity(\"first\")\n";
const char* kSecond = "var mine = Entity(\"second\")\n";

// The events and the timers.
const char* kEvents =
    "def on_coin(entity) {\n"
    "    log(\"coin\", entity.name)\n"
    "}\n"
    "def on_timer() {\n"
    "    log(\"timer\")\n"
    "}\n"
    "def on_start() {\n"
    "    on(\"coin_taken\", \"on_coin\")\n"
    "    after(0.1, \"on_timer\")\n"
    "}\n";

// The data of a project: the .spl format of the language.
const char* kData =
    "def on_start() {\n"
    "    var level = loadData(\"spstest_level.spl\")\n"
    "    log(\"level-name\", level[\"name\"])\n"
    "}\n";

} // namespace

int main() {
    // The output is not buffered: a test that crashes must have said where it
    // got to before it crashed.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    PhysicsWorld world;
    MeshRegistry meshes;
    Camera camera;
    // No window, on purpose: the whole point of this harness is that it runs
    // headless. It is also a test in itself - every input binding has to answer
    // "no" rather than dereference a null window.
    sps::Host host(world, meshes, camera, nullptr);
    host.setDiagnosticSink(collect);

    heading("initialize");
    const bool started = host.initialize();
    check(started, "initialize() succeeds");
    check(host.available(), "available() is true after initialize()");
    check(!host.engineVersion().empty(), "engineVersion() is reported");

    // --- the classes of the engine -----------------------------------------
    heading("classes");
    g_seen.clear();
    Log::clear();
    const bool ran = host.runString("full.sps", kFullScript);
    check(ran, "runString() succeeds");
    check(errorCount() == 0, "no error diagnostic");
    check(countLines("started") == 1, "the on_start entry point ran");
    check(countLines("started player hero") == 1, "the entity kept its name and its tag");
    check(world.bodies.size() == 1, "the entity made one body");
    check(host.scriptCount() == 1, "one script is loaded");
    check(host.scriptName(0) == "full.sps", "the script is named after its file");

    // --- the per-frame hooks ------------------------------------------------
    heading("hooks");
    g_seen.clear();
    Log::clear();
    check(host.callUpdate(0.016f), "callUpdate() says a script declared the hook");
    check(countLines("tick") == 1, "the on_update hook ran once");
    check(countLines("tick 0.016") == 1, "the hook was given the length of the frame");
    check(host.callFixedUpdate(0.016f), "callFixedUpdate() says a script declared the hook");
    check(countLines("step") == 1, "the on_fixed_update hook ran once");
    check(host.callStop(), "callStop() says a script declared the hook");
    check(countLines("stopped") == 1, "the on_stop hook ran once");

    // --- a script with no hooks is not a failure ---------------------------
    heading("no hooks");
    g_seen.clear();
    Log::clear();
    check(host.runString("helpers.sps", kHelpersOnly), "a script with no hooks succeeds");
    check(countLines("helper") == 0, "and runs nothing");
    check(errorCount() == 0, "and reports nothing");

    // --- two scripts live side by side --------------------------------------
    heading("two scripts");
    g_seen.clear();
    Log::clear();
    check(host.runString("first.sps", kFirst), "the first script loads");
    check(host.runString("second.sps", kSecond), "the second script loads");
    check(host.scriptCount() == 4, "both scripts are loaded beside the first two");
    check(world.bodies.size() == 3, "each script made a body of its own");

    // --- a script that does not compile -------------------------------------
    heading("compile error");
    g_seen.clear();
    Log::clear();
    const bool broke = host.runString("broken.sps", kBroken);
    check(!broke, "runString() returns false on a compile error");
    check(errorCount() > 0, "an error diagnostic was published to the sink");
    check(host.scriptCount() == 4, "the broken script was not loaded");

    // --- a script that fails at run time ------------------------------------
    heading("runtime error");
    g_seen.clear();
    Log::clear();
    check(host.runString("runtime.sps", kRuntimeFailure), "the script compiles");
    check(host.callUpdate(0.016f), "the hook is there");
    check(errorCount() > 0, "the failure was reported");
    // The runtime cannot go on with a context that has failed, so the script
    // is left alone: the second frame must not fail again.
    const int errorsAfterFirst = errorCount();
    host.callUpdate(0.016f);
    check(errorCount() == errorsAfterFirst, "the broken script is not called again");

    // --- the events and the timers ------------------------------------------
    heading("events and timers");
    g_seen.clear();
    Log::clear();
    check(host.runString("events.sps", kEvents), "the script loads");
    host.callUpdate(0.016f);
    check(countLines("timer") == 0, "the timer has not fired yet");
    // The timer is due after a tenth of a second: seven frames of a sixtieth.
    for (int i = 0; i < 10; ++i) host.callUpdate(0.016f);
    check(countLines("timer") == 1, "the timer fired once");

    // --- the data of a project ----------------------------------------------
    heading("data");
    g_seen.clear();
    Log::clear();
    {
        std::ofstream out("spstest_level.spl");
        out << "{ name = \"level_01\", coins = 3 }";
    }
    check(host.runString("data.sps", kData), "the script loads");
    check(countLines("level-name") == 1, "the data file was read");
    check(countLines("level-name level_01") == 1, "and it says what it says");
    std::remove("spstest_level.spl");

    // --- the reload ----------------------------------------------------------
    heading("reload");
    g_seen.clear();
    Log::clear();
    const size_t bodiesBefore = world.bodies.size();
    check(host.reload(), "reload() succeeds");
    check(world.bodies.size() == bodiesBefore,
          "the reload did not make a second copy of the bodies");
    check(host.scriptCount() > 0, "the scripts are loaded again");

    heading("shutdown");
    host.shutdown();
    check(!host.available(), "available() is false after shutdown()");
    check(!host.callUpdate(0.016f), "callUpdate() is false after shutdown()");

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
