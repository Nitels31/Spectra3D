#include "physics/CustomParam.hpp"

#include <cmath>
#include <cstring>

namespace s3d {

const char* paramTypeName(ParamType type) {
    switch (type) {
        case ParamType::Number: return "number";
        case ParamType::Integer: return "integer";
        case ParamType::Flag: return "flag";
        case ParamType::Text: return "text";
    }
    return "number";
}

bool paramTypeFromName(const char* name, ParamType& out) {
    if (!name) return false;
    if (std::strcmp(name, "number") == 0) {
        out = ParamType::Number;
        return true;
    }
    if (std::strcmp(name, "integer") == 0) {
        out = ParamType::Integer;
        return true;
    }
    if (std::strcmp(name, "flag") == 0) {
        out = ParamType::Flag;
        return true;
    }
    if (std::strcmp(name, "text") == 0) {
        out = ParamType::Text;
        return true;
    }
    return false;
}

float CustomParam::asNumber() const {
    switch (type) {
        case ParamType::Number:
        case ParamType::Integer:
            return number;
        case ParamType::Flag:
            return flag ? 1.0f : 0.0f;
        case ParamType::Text:
            // There is no number in a word, and pretending there is one would
            // make a script that reads a word as a number read a zero and
            // believe it.
            return 0.0f;
    }
    return 0.0f;
}

int CustomParam::asInteger() const {
    switch (type) {
        case ParamType::Number:
        case ParamType::Integer:
            return (int)std::lround(number);
        case ParamType::Flag:
            return flag ? 1 : 0;
        case ParamType::Text:
            return 0;
    }
    return 0;
}

} // namespace s3d
