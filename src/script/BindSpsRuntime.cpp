#include "script/SpsBindings.hpp"

#if SPECTRA_SPSCRIPT

#include "assets/MeshRegistry.hpp"
#include "core/Log.hpp"
#include "core/Str.hpp"
#include "gui/GuiCanvas.hpp"
#include "physics/PhysicsWorld.hpp"
#include "platform/Filesystem.hpp"
#include "platform/Window.hpp"
#include "scene/BodyFactory.hpp"
#include "scene/Raycast.hpp"

#include <algorithm>
#include <cmath>

namespace s3d {
namespace sps {
namespace {

// The script that a function belongs to.
Script& scriptOf(void* user) { return *static_cast<Script*>(user); }

// --- the logging -------------------------------------------------------------

// A line of the log: every argument is put next to the others, the way a
// person would write them.
void logLine(SpsContext* context, int argc, const SpsValue* args, const char* level) {
    std::string line;
    for (int i = 0; i < argc; ++i) {
        if (i) line += " ";
        line += textOf(context, args[i]);
    }
    if (level[0] == 'w')
        Log::warn("[script] %s", line.c_str());
    else if (level[0] == 'e')
        Log::error("[script] %s", line.c_str());
    else
        Log::info("[script] %s", line.c_str());
}

SpsValue nativeLog(SpsContext* context, int argc, const SpsValue* args, void*) {
    logLine(context, argc, args, "i");
    return sps_nil();
}

SpsValue nativeLogWarning(SpsContext* context, int argc, const SpsValue* args, void*) {
    logLine(context, argc, args, "w");
    return sps_nil();
}

SpsValue nativeLogError(SpsContext* context, int argc, const SpsValue* args, void*) {
    logLine(context, argc, args, "e");
    return sps_nil();
}

// --- the values --------------------------------------------------------------

SpsValue nativeVec3(SpsContext*, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    Vec3 value;
    if (argc > 0) value.x = (float)sps_value_to_float(args[0]);
    if (argc > 1) value.y = (float)sps_value_to_float(args[1]);
    if (argc > 2) value.z = (float)sps_value_to_float(args[2]);
    return makeVector(script, value);
}

SpsValue nativeColor(SpsContext*, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    Color value;
    if (argc > 0) value.r = (float)sps_value_to_float(args[0]);
    if (argc > 1) value.g = (float)sps_value_to_float(args[1]);
    if (argc > 2) value.b = (float)sps_value_to_float(args[2]);
    if (argc > 3) value.a = (float)sps_value_to_float(args[3]);
    return makeColor(script, value);
}

// --- the maths ---------------------------------------------------------------

SpsValue nativeLerp(SpsContext*, int argc, const SpsValue* args, void*) {
    if (argc < 3) return sps_float(0.0);
    const double a = sps_value_to_float(args[0]);
    const double b = sps_value_to_float(args[1]);
    const double t = sps_value_to_float(args[2]);
    return sps_float(a + (b - a) * t);
}

SpsValue nativeClamp(SpsContext*, int argc, const SpsValue* args, void*) {
    if (argc < 3) return sps_float(0.0);
    const double value = sps_value_to_float(args[0]);
    const double low = sps_value_to_float(args[1]);
    const double high = sps_value_to_float(args[2]);
    return sps_float(value < low ? low : (value > high ? high : value));
}

// The number inside the range, wrapping round at the top of it: an angle of
// 370 degrees is 10 degrees.
SpsValue nativeWrap(SpsContext*, int argc, const SpsValue* args, void*) {
    if (argc < 3) return sps_float(0.0);
    const double value = sps_value_to_float(args[0]);
    const double low = sps_value_to_float(args[1]);
    const double high = sps_value_to_float(args[2]);
    const double range = high - low;
    if (range == 0.0) return sps_float(low);
    return sps_float(value - range * std::floor((value - low) / range));
}

SpsValue nativeSign(SpsContext*, int argc, const SpsValue* args, void*) {
    if (argc < 1) return sps_float(0.0);
    const double value = sps_value_to_float(args[0]);
    return sps_float(value > 0.0 ? 1.0 : (value < 0.0 ? -1.0 : 0.0));
}

SpsValue nativeMoveTowards(SpsContext*, int argc, const SpsValue* args, void*) {
    if (argc < 3) return sps_float(0.0);
    const double value = sps_value_to_float(args[0]);
    const double target = sps_value_to_float(args[1]);
    const double step = sps_value_to_float(args[2]);
    if (value < target) return sps_float(value + step < target ? value + step : target);
    if (value > target) return sps_float(value - step > target ? value - step : target);
    return sps_float(target);
}

// --- the time ----------------------------------------------------------------

SpsValue nativeDeltaTime(SpsContext*, int argc, const SpsValue* args, void* user) {
    (void)argc;
    (void)args;
    Script& script = scriptOf(user);
    return sps_float(script.state->deltaTime);
}

SpsValue nativeFrame(SpsContext*, int argc, const SpsValue* args, void* user) {
    (void)argc;
    (void)args;
    Script& script = scriptOf(user);
    return sps_int((int64_t)script.state->frame);
}

// --- the input ---------------------------------------------------------------

// The keys a game asks about, named. The alternative is a script containing
// the number 87, which is GLFW_KEY_W, which nothing in the file says and
// nothing warns about when GLFW changes it.
int keyCode(const std::string& name) {
    struct Named {
        const char* name;
        int key;
    };
    static const Named kKeys[] = {
        {"a", GLFW_KEY_A},           {"b", GLFW_KEY_B},         {"c", GLFW_KEY_C},
        {"d", GLFW_KEY_D},           {"e", GLFW_KEY_E},         {"f", GLFW_KEY_F},
        {"g", GLFW_KEY_G},           {"h", GLFW_KEY_H},         {"i", GLFW_KEY_I},
        {"j", GLFW_KEY_J},           {"k", GLFW_KEY_K},         {"l", GLFW_KEY_L},
        {"m", GLFW_KEY_M},           {"n", GLFW_KEY_N},         {"o", GLFW_KEY_O},
        {"p", GLFW_KEY_P},           {"q", GLFW_KEY_Q},         {"r", GLFW_KEY_R},
        {"s", GLFW_KEY_S},           {"t", GLFW_KEY_T},         {"u", GLFW_KEY_U},
        {"v", GLFW_KEY_V},           {"w", GLFW_KEY_W},         {"x", GLFW_KEY_X},
        {"y", GLFW_KEY_Y},           {"z", GLFW_KEY_Z},         {"space", GLFW_KEY_SPACE},
        {"shift", GLFW_KEY_LEFT_SHIFT}, {"control", GLFW_KEY_LEFT_CONTROL},
        {"alt", GLFW_KEY_LEFT_ALT},  {"escape", GLFW_KEY_ESCAPE},
        {"enter", GLFW_KEY_ENTER},   {"tab", GLFW_KEY_TAB},
        {"left", GLFW_KEY_LEFT},     {"right", GLFW_KEY_RIGHT},
        {"up", GLFW_KEY_UP},         {"down", GLFW_KEY_DOWN},
        {"0", GLFW_KEY_0},           {"1", GLFW_KEY_1},         {"2", GLFW_KEY_2},
        {"3", GLFW_KEY_3},           {"4", GLFW_KEY_4},         {"5", GLFW_KEY_5},
        {"6", GLFW_KEY_6},           {"7", GLFW_KEY_7},         {"8", GLFW_KEY_8},
        {"9", GLFW_KEY_9},           {"f1", GLFW_KEY_F1},       {"f2", GLFW_KEY_F2},
        {"f3", GLFW_KEY_F3},         {"f4", GLFW_KEY_F4},
    };

    for (const Named& entry : kKeys)
        if (str::equalsIgnoreAscii(entry.name, name)) return entry.key;
    return -1;
}

SpsValue nativeDownKey(SpsContext* context, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    if (argc < 1 || !script.state->window) return sps_bool(false);
    const int key = keyCode(textOf(context, args[0]));
    if (key < 0) return sps_bool(false);
    return sps_bool(script.state->window->keyDown(key));
}

// Was the key pressed during this frame? The queue of the frame's edges is
// read and not taken, so the editor and the script both see the press.
SpsValue nativeJustPressed(SpsContext* context, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    if (argc < 1 || !script.state->window) return sps_bool(false);
    const int key = keyCode(textOf(context, args[0]));
    if (key < 0) return sps_bool(false);

    const int count = script.state->window->eventCount();
    for (int i = 0; i < count; ++i) {
        InputEvent event;
        if (!script.state->window->eventAt(i, event)) continue;
        if (event.type != InputEventType::Key) continue;
        if (event.code == key) return sps_bool(true);
    }
    return sps_bool(false);
}

SpsValue nativeMouseX(SpsContext*, int argc, const SpsValue* args, void* user) {
    (void)argc;
    (void)args;
    Script& script = scriptOf(user);
    if (!script.state->window) return sps_float(0.0);
    double x = 0.0, y = 0.0;
    script.state->window->cursorPos(x, y);
    return sps_float((float)x);
}

SpsValue nativeMouseY(SpsContext*, int argc, const SpsValue* args, void* user) {
    (void)argc;
    (void)args;
    Script& script = scriptOf(user);
    if (!script.state->window) return sps_float(0.0);
    double x = 0.0, y = 0.0;
    script.state->window->cursorPos(x, y);
    return sps_float((float)y);
}

SpsValue nativeMouseDown(SpsContext*, int argc, const SpsValue* args, void* user) {
    (void)argc;
    Script& script = scriptOf(user);
    if (!script.state->window) return sps_bool(false);
    const int button = (int)sps_value_to_int(args[0]);
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return sps_bool(false);
    return sps_bool(script.state->window->mouseDown(button));
}

// --- the sound ---------------------------------------------------------------

SpsValue nativePlaySound(SpsContext* context, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    if (argc < 1 || !script.state->sound) return sps_nil();
    const std::string name = textOf(context, args[0]);
    const float volume = argc > 1 ? (float)sps_value_to_float(args[1]) : 1.0f;
    const float pitch = argc > 2 ? (float)sps_value_to_float(args[2]) : 1.0f;
    script.state->sound->play(name, volume, pitch);
    return sps_nil();
}

SpsValue nativeStopSound(SpsContext* context, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    if (argc < 1 || !script.state->sound) return sps_nil();
    script.state->sound->stop(textOf(context, args[0]));
    return sps_nil();
}

SpsValue nativeSoundPlaying(SpsContext* context, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    if (argc < 1 || !script.state->sound) return sps_bool(false);
    return sps_bool(script.state->sound->isPlaying(textOf(context, args[0])));
}

SpsValue nativeSetMasterVolume(SpsContext*, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    if (argc < 1 || !script.state->sound) return sps_nil();
    script.state->sound->setMasterVolume(clampf((float)sps_value_to_float(args[0]), 0.0f, 1.0f));
    return sps_nil();
}

// --- the scene ---------------------------------------------------------------

int spawnBody(Script& script, Shape shape, const Vec3& position, const Vec3& halfExtents,
              float density) {
    SpsState& state = *script.state;
    if (!state.world) return -1;
    const int body = BodyFactory::spawn(*state.world, shape, position, Vec3{}, halfExtents,
                                         density, Vec3{}, Quat{});
    if (body < 0) return -1;
    ensureBodyTables(state);
    state.created.push_back(body);
    return body;
}

SpsValue nativeSpawnBox(SpsContext*, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    const Vec3 position(argc > 0 ? (float)sps_value_to_float(args[0]) : 0.0f,
                        argc > 1 ? (float)sps_value_to_float(args[1]) : 0.0f,
                        argc > 2 ? (float)sps_value_to_float(args[2]) : 0.0f);
    const Vec3 size(argc > 3 ? (float)sps_value_to_float(args[3]) : 1.0f,
                    argc > 4 ? (float)sps_value_to_float(args[4]) : 1.0f,
                    argc > 5 ? (float)sps_value_to_float(args[5]) : 1.0f);
    const float density = argc > 6 ? (float)sps_value_to_float(args[6]) : 1.0f;
    const int body = spawnBody(script, Shape::Box, position, size * 0.5f, density);
    return sps_int((int64_t)body);
}

SpsValue nativeSpawnSphere(SpsContext*, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    const Vec3 position(argc > 0 ? (float)sps_value_to_float(args[0]) : 0.0f,
                        argc > 1 ? (float)sps_value_to_float(args[1]) : 0.0f,
                        argc > 2 ? (float)sps_value_to_float(args[2]) : 0.0f);
    const float radius = argc > 3 ? (float)sps_value_to_float(args[3]) : 0.5f;
    const float density = argc > 4 ? (float)sps_value_to_float(args[4]) : 1.0f;
    const int body = spawnBody(script, Shape::Sphere, position, Vec3{radius, radius, radius},
                                density);
    return sps_int((int64_t)body);
}

SpsValue nativeSpawnShape(SpsContext*, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    if (argc < 1) return sps_int(-1);

    // The shapes are the ones the engine writes into a scene, so a script and
    // a scene file say the same thing about a body.
    static const struct {
        const char* name;
        Shape shape;
    } kShapes[] = {
        {"box", Shape::Box},         {"sphere", Shape::Sphere},   {"cylinder", Shape::Cylinder},
        {"capsule", Shape::Capsule}, {"cone", Shape::Cone},       {"pyramid", Shape::Pyramid},
        {"prism", Shape::Prism},     {"quad", Shape::Quad},
    };

    const std::string wanted = textOf(script.context, args[0]);
    Shape shape = Shape::Box;
    bool known = false;
    for (const auto& entry : kShapes) {
        if (!str::equalsIgnoreAscii(entry.name, wanted)) continue;
        shape = entry.shape;
        known = true;
        break;
    }
    if (!known) {
        Log::error("sps: spawnShape: '%s' is not a shape", wanted.c_str());
        return sps_int(-1);
    }

    const Vec3 position(argc > 1 ? (float)sps_value_to_float(args[1]) : 0.0f,
                        argc > 2 ? (float)sps_value_to_float(args[2]) : 0.0f,
                        argc > 3 ? (float)sps_value_to_float(args[3]) : 0.0f);
    const Vec3 size(argc > 4 ? (float)sps_value_to_float(args[4]) : 1.0f,
                    argc > 5 ? (float)sps_value_to_float(args[5]) : 1.0f,
                    argc > 6 ? (float)sps_value_to_float(args[6]) : 1.0f);
    const float density = argc > 7 ? (float)sps_value_to_float(args[7]) : 1.0f;
    const int body = spawnBody(script, shape, position, size * 0.5f, density);
    return sps_int((int64_t)body);
}

SpsValue nativeRemoveBody(SpsContext*, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    if (argc < 1) return sps_nil();
    removeBody(*script.state, (int)sps_value_to_int(args[0]));
    return sps_nil();
}

SpsValue nativeBodyCount(SpsContext*, int argc, const SpsValue* args, void* user) {
    (void)argc;
    (void)args;
    Script& script = scriptOf(user);
    if (!script.state->world) return sps_int(0);
    return sps_int((int64_t)script.state->world->bodies.size());
}

SpsValue nativeClearScene(SpsContext*, int argc, const SpsValue* args, void* user) {
    (void)argc;
    (void)args;
    Script& script = scriptOf(user);
    clearScene(*script.state);
    return sps_nil();
}

SpsValue nativeSetGravity(SpsContext*, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    if (argc < 1 || !script.state->world) return sps_nil();
    const int axis = (int)sps_value_to_int(args[0]);
    if (axis < (int)GravityAxis::NegX || axis > (int)GravityAxis::PosZ) {
        Log::error("sps: setGravity: %d is not a gravity axis (0..5)", axis);
        return sps_nil();
    }
    script.state->world->setGravity((GravityAxis)axis);
    return sps_nil();
}

SpsValue nativeGetGravity(SpsContext*, int argc, const SpsValue* args, void* user) {
    (void)argc;
    (void)args;
    Script& script = scriptOf(user);
    if (!script.state->world) return sps_int(-1);
    return sps_int((int64_t)script.state->world->gravityAxis);
}

// A model of the assets, as a body of the world.
SpsValue nativeLoadModel(SpsContext* context, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    SpsState& state = *script.state;
    if (argc < 1 || !state.world || !state.meshes) return sps_nil();

    const std::string path = textOf(context, args[0]);
    const int mesh = state.meshes->importFile(path);
    if (mesh < 0) return sps_nil();

    const MeshAsset* asset = state.meshes->at(mesh);
    const Vec3 half = asset ? asset->half : Vec3{0.5f, 0.5f, 0.5f};

    const int body = spawnBody(script, Shape::Mesh, Vec3{}, half, 1.0f);
    if (body < 0) return sps_nil();

    RigidBody* rigid = state.world->find(body);
    if (rigid) {
        rigid->mesh = mesh;
        rigid->meshScale = asset ? asset->scale : 1.0f;
        rigid->configure(rigid->mass);
    }
    return makeEntity(script, body);
}

// The ray: what it finds is a list of the body, the point, the face and the
// distance.
SpsValue nativeRaycast(SpsContext*, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    SpsState& state = *script.state;
    if (argc < 2 || !state.world) return sps_nil();

    Vec3 origin;
    Vec3 direction;
    if (args[0].type == SPS_OBJECT || args[0].type == SPS_USERDATA) {
        origin = vectorOf(args[0]);
        direction = vectorOf(args[1]);
    } else {
        origin = Vec3(argc > 0 ? (float)sps_value_to_float(args[0]) : 0.0f,
                      argc > 1 ? (float)sps_value_to_float(args[1]) : 0.0f,
                      argc > 2 ? (float)sps_value_to_float(args[2]) : 0.0f);
        direction = Vec3(argc > 3 ? (float)sps_value_to_float(args[3]) : 0.0f,
                         argc > 4 ? (float)sps_value_to_float(args[4]) : 0.0f,
                         argc > 5 ? (float)sps_value_to_float(args[5]) : 0.0f);
    }
    const float maxDistance = argc > 2 ? (float)sps_value_to_float(args[2]) : 1000.0f;

    scene::RaycastHit hit;
    if (!scene::raycast(origin, direction, maxDistance, *state.world, hit)) return sps_nil();

    SpsValue list = sps_value_list(script.context, 0, nullptr);
    sps_value_list_append(script.context, list, makeEntity(script, hit.body));
    sps_value_list_append(script.context, list, makeVector(script, hit.point));
    sps_value_list_append(script.context, list, makeVector(script, hit.normal));
    sps_value_list_append(script.context, list, sps_float(hit.distance));
    return list;
}

// --- the events --------------------------------------------------------------

SpsValue nativeOn(SpsContext* context, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    if (argc < 2) return sps_nil();
    const std::string event = textOf(context, args[0]);
    const std::string function = textOf(context, args[1]);

    std::vector<std::string>& list = script.state->handlers[event];
    for (const std::string& entry : list)
        if (entry == function) return sps_nil();
    list.push_back(function);
    return sps_nil();
}

SpsValue nativeOff(SpsContext* context, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    if (argc < 2) return sps_nil();
    const std::string event = textOf(context, args[0]);
    const std::string function = textOf(context, args[1]);

    std::vector<std::string>& list = script.state->handlers[event];
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i] != function) continue;
        list.erase(list.begin() + (long)i);
        break;
    }
    return sps_nil();
}

SpsValue nativeEmit(SpsContext* context, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    if (argc < 1) return sps_nil();

    const std::string event = textOf(context, args[0]);
    Payload payload;
    if (argc > 1) {
        // A body is a number of the world and every script can make its own
        // object out of it. Anything else is a number or a word, and those
        // mean the same thing everywhere.
        const int body = entityIndexOf(args[1]);
        if (body >= 0) {
            payload.body = body;
        } else {
            const SpsType type = sps_value_type(args[1]);
            if (type == SPS_INT || type == SPS_FLOAT || type == SPS_BOOL || type == SPS_STRING)
                payload.value = args[1];
        }
    }

    const std::vector<std::string> handlers = script.state->handlers.count(event)
                                                  ? script.state->handlers[event]
                                                  : std::vector<std::string>();
    for (const std::string& function : handlers) emitEvent(script, function, payload);
    return sps_nil();
}

// --- the timers --------------------------------------------------------------

SpsValue nativeAfter(SpsContext* context, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    if (argc < 2) return sps_int(0);

    SpsState::Timer timer;
    timer.id = script.state->nextTimerId++;
    timer.function = textOf(context, args[1]);
    timer.interval = sps_value_to_float(args[0]);
    timer.remaining = timer.interval;
    timer.repeat = false;
    script.state->timers.push_back(timer);
    return sps_int((int64_t)timer.id);
}

SpsValue nativeEvery(SpsContext* context, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    if (argc < 2) return sps_int(0);

    SpsState::Timer timer;
    timer.id = script.state->nextTimerId++;
    timer.function = textOf(context, args[1]);
    timer.interval = sps_value_to_float(args[0]);
    timer.remaining = timer.interval;
    timer.repeat = true;
    script.state->timers.push_back(timer);
    return sps_int((int64_t)timer.id);
}

SpsValue nativeCancelTimer(SpsContext*, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    if (argc < 1) return sps_nil();
    const int id = (int)sps_value_to_int(args[0]);
    std::vector<SpsState::Timer>& timers = script.state->timers;
    timers.erase(std::remove_if(timers.begin(), timers.end(),
                                 [&](const SpsState::Timer& timer) { return timer.id == id; }),
                 timers.end());
    return sps_nil();
}

// --- the data ----------------------------------------------------------------
//
// The .spl format of Spectra3D: the same text the C++ header of the language
// reads and writes. It is what a project carries instead of JSON.

std::string dataPathOf(SpsState& state, const std::string& name) {
    // A path that says where it is is used as it is.
    const bool absolute = name.size() > 1 && name[1] == ':';
    if (absolute || name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
        return name;

    if (!state.projectRoot.empty()) {
        const std::vector<std::string> folders = {
            str::join(state.projectRoot, "assets/data"),
            str::join(state.projectRoot, "assets"),
            str::join(state.projectRoot, "data"),
        };
        for (const std::string& folder : folders) {
            const std::string path = str::join(folder, name);
            if (fs::exists(path)) return path;
        }
    }
    return name;
}

SpsValue nativeLoadData(SpsContext* context, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    if (argc < 1) return sps_nil();

    const std::string path = dataPathOf(*script.state, textOf(context, args[0]));
    std::string text;
    if (!fs::readFile(path, text)) {
        Log::error("sps: loadData: '%s' could not be read", path.c_str());
        return sps_nil();
    }
    return sps_spl_parse(script.context, text.c_str());
}

SpsValue nativeSaveData(SpsContext* context, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    if (argc < 2) return sps_nil();

    const std::string path = dataPathOf(*script.state, textOf(context, args[0]));
    const char* text = sps_value_to_text(script.context, args[1]);
    if (!text) return sps_nil();
    if (!fs::writeFile(path, text)) {
        Log::error("sps: saveData: '%s' could not be written", path.c_str());
        return sps_nil();
    }
    return sps_nil();
}

// --- the functions of the other scripts --------------------------------------

SpsValue nativeCallFunction(SpsContext* context, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    if (argc < 1) return sps_nil();

    const std::string name = textOf(context, args[0]);
    const SpsValue result = callFunctionByName(script, name, argc - 1, args + 1);
    return result;
}

// --- the registration --------------------------------------------------------

struct Native {
    const char* name;
    SpsNativeFn function;
};

const Native kFunctions[] = {
    {"log", nativeLog},
    {"logWarning", nativeLogWarning},
    {"logError", nativeLogError},

    {"vec3", nativeVec3},
    {"color", nativeColor},

    {"lerp", nativeLerp},
    {"clamp", nativeClamp},
    {"wrap", nativeWrap},
    {"sign", nativeSign},
    {"moveTowards", nativeMoveTowards},

    {"deltaTime", nativeDeltaTime},
    {"frame", nativeFrame},

    {"downKey", nativeDownKey},
    {"justPressed", nativeJustPressed},
    {"mouseX", nativeMouseX},
    {"mouseY", nativeMouseY},
    {"mouseDown", nativeMouseDown},

    {"playSound", nativePlaySound},
    {"stopSound", nativeStopSound},
    {"soundPlaying", nativeSoundPlaying},
    {"setMasterVolume", nativeSetMasterVolume},

    {"spawnBox", nativeSpawnBox},
    {"spawnSphere", nativeSpawnSphere},
    {"spawnShape", nativeSpawnShape},
    {"removeBody", nativeRemoveBody},
    {"bodyCount", nativeBodyCount},
    {"clearScene", nativeClearScene},
    {"setGravity", nativeSetGravity},
    {"getGravity", nativeGetGravity},
    {"loadModel", nativeLoadModel},
    {"raycast", nativeRaycast},

    {"on", nativeOn},
    {"off", nativeOff},
    {"emit", nativeEmit},

    {"after", nativeAfter},
    {"every", nativeEvery},
    {"cancelTimer", nativeCancelTimer},

    {"loadData", nativeLoadData},
    {"saveData", nativeSaveData},

    {"callFunction", nativeCallFunction},
};

} // namespace

bool registerFunctions(Script& script) {
    for (const Native& native : kFunctions)
        sps_register_function(script.context, native.name, native.function, &script);
    return true;
}

bool registerScript(Script& script) {
    if (!registerClasses(script)) return false;
    return registerFunctions(script);
}

} // namespace sps
} // namespace s3d

#endif // SPECTRA_SPSCRIPT
