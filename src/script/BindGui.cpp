#include "script/Bindings.hpp"

#include "core/Log.hpp"
#include "core/Math.hpp"
#include "gui/GuiCanvas.hpp"
#include "script/BindString.hpp"

#if SPECTRA_SCRIPTING

namespace s3d {
namespace script {
namespace {

// --- engine:: the interface of the game ---------------------------------------
//
// The canvas of the game: the panels, the pictures, the labels and the buttons.
// A script asks for an element by its name and says what should happen to it.
//
// The names are the ones the editor gave the elements, and they travel with
// the scene: an element that is called "Score" in the editor is called "Score"
// in the script and in the file.
//
// Every binding here answers quietly when there is no canvas. A machine that
// has no interface is a machine where the game still has to run, and a script
// that asks a label to change in a game that has no labels is not a mistake
// worth a crash.

// The element with this name, or nothing. A name nobody carries is a name a
// script made up, and it is said once in the log rather than every frame.
gui::Element* elementByName(const char* fn, const ScriptString& name) {
    ScriptState* state = activeState();
    if (!state || !state->canvas) return nullptr;
    const int index = state->canvas->find(name.str());
    if (index < 0) {
        Log::error("engine::%s: there is no element called '%s'", fn, name.c_str());
        return nullptr;
    }
    return state->canvas->at(index);
}

// The same, without the complaining: for the questions a script asks every
// frame, where a name that is not there is an answer of "no".
gui::Element* elementByNameQuiet(const ScriptString& name) {
    ScriptState* state = activeState();
    if (!state || !state->canvas) return nullptr;
    return state->canvas->at(state->canvas->find(name.str()));
}

void setText(const ScriptString& name, const ScriptString& text) {
    gui::Element* element = elementByName("uiSetText", name);
    if (!element) return;
    element->text = text.str();
}

void setVisible(const ScriptString& name, bool visible) {
    gui::Element* element = elementByName("uiSetVisible", name);
    if (!element) return;
    element->visible = visible;
}

bool isVisible(const ScriptString& name) {
    gui::Element* element = elementByNameQuiet(name);
    return element ? element->visible : false;
}

// Was the button pressed this frame? The renderer marks the element, and the
// mark is the answer.
bool wasClicked(const ScriptString& name) {
    gui::Element* element = elementByNameQuiet(name);
    return element ? element->clicked : false;
}

bool isHovered(const ScriptString& name) {
    gui::Element* element = elementByNameQuiet(name);
    return element ? element->hovered : false;
}

void setColor(const ScriptString& name, float r, float g, float b, float a) {
    gui::Element* element = elementByName("uiSetColor", name);
    if (!element) return;
    element->color.r = clampf(r, 0.0f, 1.0f);
    element->color.g = clampf(g, 0.0f, 1.0f);
    element->color.b = clampf(b, 0.0f, 1.0f);
    element->color.a = clampf(a, 0.0f, 1.0f);
}

void setPosition(const ScriptString& name, float x, float y) {
    gui::Element* element = elementByName("uiSetPosition", name);
    if (!element) return;
    element->x = x;
    element->y = y;
}

void setSize(const ScriptString& name, float width, float height) {
    gui::Element* element = elementByName("uiSetSize", name);
    if (!element) return;
    element->width = width > 0.0f ? width : 1.0f;
    element->height = height > 0.0f ? height : 1.0f;
}

// How many elements the canvas has, and how many of them are visible: the two
// numbers a script asks when it wants to know what is on the screen.
int elementCount() {
    ScriptState* state = activeState();
    if (!state || !state->canvas) return 0;
    return (int)state->canvas->elements.size();
}

int visibleCount() {
    ScriptState* state = activeState();
    if (!state || !state->canvas) return 0;
    int count = 0;
    for (const gui::Element& element : state->canvas->elements)
        if (element.visible) ++count;
    return count;
}

const Binding kBindings[] = {
    {"void uiSetText(const string &in name, const string &in text)", asFUNCTION(setText)},
    {"void uiSetVisible(const string &in name, bool visible)", asFUNCTION(setVisible)},
    {"bool uiIsVisible(const string &in name)", asFUNCTION(isVisible)},
    {"bool uiWasClicked(const string &in name)", asFUNCTION(wasClicked)},
    {"bool uiIsHovered(const string &in name)", asFUNCTION(isHovered)},
    {"void uiSetColor(const string &in name, float r, float g, float b, float a)",
     asFUNCTION(setColor)},
    {"void uiSetPosition(const string &in name, float x, float y)", asFUNCTION(setPosition)},
    {"void uiSetSize(const string &in name, float width, float height)", asFUNCTION(setSize)},
    {"int uiElementCount()", asFUNCTION(elementCount)},
    {"int uiVisibleCount()", asFUNCTION(visibleCount)},
};

} // namespace

int registerGuiBindings(asIScriptEngine* engine) {
    return registerBindings(engine, kBindings, sizeof(kBindings) / sizeof(kBindings[0]));
}

} // namespace script
} // namespace s3d

#endif  // SPECTRA_SCRIPTING
