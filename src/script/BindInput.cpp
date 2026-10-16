#include "script/Bindings.hpp"

#include "platform/Window.hpp"

#include <cstdio>

#if SPECTRA_SCRIPTING

namespace s3d {
namespace script {
namespace {

// The keys a game actually asks about, named. The alternative is a script
// containing the number 87, which is GLFW_KEY_W, which nothing in the file says
// and nothing warns about when GLFW changes it.
//
// Registered as global properties rather than an AngelScript enum, and isKeyDown()
// takes an int on purpose: a script can then ask about a key this list has not
// heard of without the call failing to compile, which matters more than the type
// safety of a set frozen at the moment the engine was built.
//
// Not const on the storage: RegisterGlobalProperty takes a void* and the engine
// only ever reads through it, but handing it a pointer to something declared
// const needs a const_cast, and a const_cast that lies is the kind of thing that
// stops being a lie later.
void keyConstants(asIScriptEngine* engine) {
    struct Named {
        const char* name;
        int key;
    };
    static Named kKeys[] = {
        {"KeyA", GLFW_KEY_A},
        {"KeyB", GLFW_KEY_B},
        {"KeyC", GLFW_KEY_C},
        {"KeyD", GLFW_KEY_D},
        {"KeyE", GLFW_KEY_E},
        {"KeyF", GLFW_KEY_F},
        {"KeyG", GLFW_KEY_G},
        {"KeyQ", GLFW_KEY_Q},
        {"KeyR", GLFW_KEY_R},
        {"KeyS", GLFW_KEY_S},
        {"KeyW", GLFW_KEY_W},
        {"KeyZ", GLFW_KEY_Z},
        {"Space", GLFW_KEY_SPACE},
        {"Shift", GLFW_KEY_LEFT_SHIFT},
        {"Control", GLFW_KEY_LEFT_CONTROL},
        {"Alt", GLFW_KEY_LEFT_ALT},
        {"Escape", GLFW_KEY_ESCAPE},
        {"Enter", GLFW_KEY_ENTER},
        {"Tab", GLFW_KEY_TAB},
        {"Left", GLFW_KEY_LEFT},
        {"Right", GLFW_KEY_RIGHT},
        {"Up", GLFW_KEY_UP},
        {"Down", GLFW_KEY_DOWN},
        {"Digit0", GLFW_KEY_0},
        {"Digit1", GLFW_KEY_1},
        {"Digit2", GLFW_KEY_2},
        {"Digit3", GLFW_KEY_3},
        {"Digit4", GLFW_KEY_4},
        {"Digit5", GLFW_KEY_5},
        {"Digit6", GLFW_KEY_6},
        {"Digit7", GLFW_KEY_7},
        {"Digit8", GLFW_KEY_8},
        {"Digit9", GLFW_KEY_9},
        {"F1", GLFW_KEY_F1},
        {"F2", GLFW_KEY_F2},
        {"F3", GLFW_KEY_F3},
        {"F4", GLFW_KEY_F4},
    };

    // RegisterGlobalProperty takes a DECLARATION, not a name: handing it "KeyA"
    // fails as an invalid declaration, once per key, and the message reads like
    // the script's fault.
    char declaration[32];
    for (Named& named : kKeys) {
        std::snprintf(declaration, sizeof(declaration), "const int %s", named.name);
        engine->RegisterGlobalProperty(declaration, &named.key);
    }
}

// Level-triggered: "is the key down right now", which is what movement in a fixed
// step needs. An edge - "was it pressed this frame" - would fire once and then go
// quiet, and a character holding W would move for one step and stop.
bool isKeyDown(int key) {
    ScriptState* state = activeState();
    if (!state || !state->window) return false;
    // An out-of-range key is a script bug, not a crash: the array is indexed by
    // the value, and the value came from a script.
    if (key < 0 || key > GLFW_KEY_LAST) return false;
    return state->window->keyDown(key);
}

bool isMouseButtonDown(int button) {
    ScriptState* state = activeState();
    if (!state || !state->window) return false;
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return false;
    return state->window->mouseDown(button);
}

// The movement since the last reader, in window pixels. CONSUMING, not peeking:
// Window::mouseDelta() clears the accumulator, so a script that reads it and the
// editor that reads it cannot both see the same movement, and a script that
// polls it every frame does not see the same movement twice.
//
// Consequence worth stating: only ONE thing may read the delta per frame. A
// capturing script takes the mouse and must not also have the editor looking over
// its shoulder.
bool getMouseDelta(float& dx, float& dy) {
    ScriptState* state = activeState();
    if (!state || !state->window) {
        dx = 0.0f;
        dy = 0.0f;
        return false;
    }
    double moveX = 0.0, moveY = 0.0;
    state->window->mouseDelta(moveX, moveY);
    dx = (float)moveX;
    dy = (float)moveY;
    return moveX != 0.0 || moveY != 0.0;
}

void setMouseCaptured(bool capture) {
    ScriptState* state = activeState();
    if (!state || !state->window) return;
    state->window->setMouseCaptured(capture);
}

bool isMouseCaptured() {
    ScriptState* state = activeState();
    return state && state->window && state->window->mouseCaptured();
}

double getScrollDelta() {
    ScriptState* state = activeState();
    if (!state || !state->window) return 0.0;
    // Also consuming, for the same reason as the mouse delta: Window::consumeScroll
    // empties the accumulator, and the editor console does not scroll on it today,
    // so a script reading it takes it out of nobody's hands.
    return state->window->consumeScroll();
}

// --- declarations -----------------------------------------------------------
//
// No namespace in the strings: the host points the engine's default namespace at
// `engine` first, so these land there and scripts spell them engine::isKeyDown.

const Binding kBindings[] = {
    {"bool isKeyDown(int key)", asFUNCTION(isKeyDown)},
    {"bool isMouseButtonDown(int button)", asFUNCTION(isMouseButtonDown)},
    // `&out`, not a bare `&`. A bare `&` is `inout`, and AngelScript rejects
    // inout on anything that is not an object handle: "Only object types that
    // support object handles can use &inout". A float is a primitive, so the
    // declaration has to say which direction it goes.
    {"bool getMouseDelta(float &out dx, float &out dy)", asFUNCTION(getMouseDelta)},
    {"void setMouseCaptured(bool capture)", asFUNCTION(setMouseCaptured)},
    {"bool isMouseCaptured()", asFUNCTION(isMouseCaptured)},
    {"double getScrollDelta()", asFUNCTION(getScrollDelta)},
};

} // namespace

int registerInputBindings(asIScriptEngine* engine) {
    keyConstants(engine);
    return registerBindings(engine, kBindings, sizeof(kBindings) / sizeof(kBindings[0]));
}

} // namespace script
} // namespace s3d

#endif // SPECTRA_SCRIPTING
