#pragma once
#include <string>

namespace s3d {

// What a custom parameter holds.
//
// A parameter is a value a person puts on a thing in the scene: the health
// of a crate, the name of the player, whether a door is open. The engine
// knows nothing about any of them - it carries them, saves them and hands
// them to the scripts - but it does know what KIND of value each one is,
// because a number and a word are edited in different ways.
enum class ParamType {
    Number,  // a number with a fraction in it
    Integer, // a whole number
    Flag,    // yes or no
    Text,    // a word
};

// The number of the kinds, so a cycle through them can wrap around.
constexpr int kParamTypeCount = 4;

// The name of the kind, as it is written in a scene file.
const char* paramTypeName(ParamType type);

// The kind with this name, and whether there is one. A kind that is not in
// the file is a kind that was never written, and the file is read anyway.
bool paramTypeFromName(const char* name, ParamType& out);

// A PARAMETER OF A THING
//
// One value with a name on it. The name is what a script asks for, so it is
// the identity of the parameter and not a label: two parameters with the same
// name are the same parameter.
struct CustomParam {
    // The name of it. Empty is not a name and not a parameter either - a
    // parameter without a name is a value nobody can ask for.
    std::string name;
    // What it holds.
    ParamType type = ParamType::Number;
    // The number, for the two number kinds.
    float number = 0.0f;
    // The yes or no, for the flag.
    bool flag = false;
    // The word, for the text.
    std::string text;

    // The value as a number, whatever it is: a flag is one or zero, and a
    // word is zero, because there is no number in a word.
    float asNumber() const;

    // The value as a whole number, whatever it is.
    int asInteger() const;
};

} // namespace s3d
