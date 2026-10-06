#include "script/Bindings.hpp"

#include "script/BindString.hpp"

#if SPECTRA_SCRIPTING

namespace s3d {
namespace script {

// --- behaviour --------------------------------------------------------------
//
// Defined out of line so the header stays a plain data description that can be
// dropped into raw engine-allocated memory.

ScriptString& ScriptString::set(const ScriptString& in) {
    create(in.c_str(), in.length());
    return *this;
}

// A reference type cannot be returned by value, so opAdd hands back a reference
// to a fresh heap object that the engine releases when the reference dies.
ScriptString& ScriptString::opAdd(const ScriptString& in) const {
    ScriptString* out = new ScriptString();
    if (!out->reserve(size + in.size + 1)) return *out;
    if (size) memcpy(out->buffer, buffer, size);
    if (in.size) memcpy(out->buffer + size, in.buffer, in.size);
    out->buffer[size + in.size] = '\0';
    out->size = size + in.size;
    return *out;
}

int ScriptString::opCmp(const ScriptString& in) const {
    const unsigned int shared = size < in.size ? size : in.size;
    const int head = shared ? memcmp(buffer, in.buffer, shared) : 0;
    if (head != 0) return head;
    // Same prefix, so the shorter one sorts first.
    return size < in.size ? -1 : (size > in.size ? 1 : 0);
}

bool ScriptString::opEquals(const ScriptString& in) const {
    return size == in.size && (size == 0 || memcmp(buffer, in.buffer, size) == 0);
}

void ScriptString::setLength(unsigned int wanted) {
    if (wanted == size) return;
    // Truncation pads with NUL, growth only succeeds if the buffer can grow.
    if (!reserve(wanted + 1)) return;
    if (wanted < size && buffer) buffer[wanted] = '\0';
    size = wanted;
}

namespace {

ScriptString* asString(const void* object) {
    return static_cast<ScriptString*>(const_cast<void*>(object));
}

// The compiler holds each literal for the whole module and pushes its pointer
// straight to whoever asked for it - without ever taking a reference of its own.
// So a literal is marked `constant`: addref/release on it are no-ops and this
// factory is the only thing that frees it, exactly once.
class StringFactory : public asIStringFactory {
public:
    const void* GetStringConstant(const char* data, asUINT length) override {
        ScriptString* text = new ScriptString();
        text->create(data, (unsigned int)length);
        text->constant = true;
        return text;
    }

    int ReleaseStringConstant(const void* str) override {
        ScriptString* text = asString(str);
        if (!text) return 0;
        text->destroy();
        delete text;
        return 1;
    }

    int GetRawStringData(const void* str, char* data, asUINT* length) const override {
        const ScriptString* text = asString(str);
        if (!text || !length) return 0;
        if (data && text->size) memcpy(data, text->buffer, text->size);
        *length = (asUINT)text->size;
        return 1;
    }
};

// The engine keeps this pointer, not a copy, so it has to outlive the engine.
// A function local static also keeps the order of destruction favourable.
StringFactory& stringFactoryInstance() {
    static StringFactory factory;
    return factory;
}

// --- behaviour, as plain free functions ------------------------------------
//
// RegisterObjectBehaviour and RegisterObjectMethod take any function pointer,
// and asCALL_CDECL_OBJFIRST passes the receiver as the first parameter. That
// keeps these ordinary C++ functions: asMETHODPR cannot express a reference
// return type, and a member function would have to lose its const to survive
// that macro's cast.
//
// Factories are the exception - they are called as plain global functions, so
// they use asCALL_CDECL and receive no receiver. Registering the one-argument
// overload as well turns the engine's copy path into "call this", which keeps
// every instance of the type allocated and freed by the same new/delete pair.

ScriptString* stringFactory() { return new ScriptString(); }
ScriptString* stringCopyFactory(const ScriptString& in) {
    ScriptString* out = new ScriptString();
    out->set(in);
    return out;
}

void stringAddRef(ScriptString& self) { self.addRef(); }
void stringRelease(ScriptString& self) { self.releaseSelf(); }
ScriptString& stringAssign(ScriptString& self, const ScriptString& in) { return self.set(in); }
ScriptString& stringAdd(ScriptString& self, const ScriptString& in) { return self.opAdd(in); }
int stringCompare(ScriptString& self, const ScriptString& in) { return self.opCmp(in); }
bool stringEquals(ScriptString& self, const ScriptString& in) { return self.opEquals(in); }
unsigned int stringLength(ScriptString& self) { return self.length(); }
void stringSetLength(ScriptString& self, unsigned int length) { self.setLength(length); }

} // namespace

int registerStringType(asIScriptEngine* engine) {
    // Reference type with the reference-counted behaviours below. `string` stays
    // in the global namespace so scripts can name it unqualified inside engine::
    // too, and so RegisterStringFactory() resolves it while the engine's default
    // namespace is still the global one.
    if (engine->RegisterObjectType("string", sizeof(ScriptString), asOBJ_REF) < 0)
        return asNO_MODULE;

    // Both factories have to return a handle: asBEHAVE_FACTORY rejects anything
    // but `Type@`, which is what the engine stores for a reference object.
    if (engine->RegisterObjectBehaviour("string", asBEHAVE_FACTORY, "string@ f()",
                                        asFUNCTION(stringFactory), asCALL_CDECL) < 0)
        return asNO_MODULE;
    if (engine->RegisterObjectBehaviour("string", asBEHAVE_FACTORY,
                                        "string@ f(const string &in)",
                                        asFUNCTION(stringCopyFactory), asCALL_CDECL) < 0)
        return asNO_MODULE;

    if (engine->RegisterObjectBehaviour("string", asBEHAVE_ADDREF, "void addRef()",
                                        asFUNCTION(stringAddRef),
                                        asCALL_CDECL_OBJFIRST) < 0)
        return asNO_MODULE;
    if (engine->RegisterObjectBehaviour("string", asBEHAVE_RELEASE, "void release()",
                                        asFUNCTION(stringRelease),
                                        asCALL_CDECL_OBJFIRST) < 0)
        return asNO_MODULE;

    // The rest is convenience for scripts: `a + b`, `a == b`, `s.length()`. The
    // const on the read-only half matters: a string literal is read-only, so
    // without it `"a" + "b"` would not match anything.
    const Binding methods[] = {
        {"string &opAssign(const string &in)", asFUNCTION(stringAssign)},
        {"string &opAdd(const string &in) const", asFUNCTION(stringAdd)},
        {"string &opAdd_r(const string &in) const", asFUNCTION(stringAdd)},
        {"int opCmp(const string &in) const", asFUNCTION(stringCompare)},
        {"bool opEquals(const string &in) const", asFUNCTION(stringEquals)},
        {"uint getLength() const", asFUNCTION(stringLength)},
        {"void setLength(uint)", asFUNCTION(stringSetLength)},
    };

    for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i)
        if (engine->RegisterObjectMethod("string", methods[i].declaration, methods[i].function,
                                         asCALL_CDECL_OBJFIRST) < 0)
            return asNO_MODULE;

    return engine->RegisterStringFactory("string", &stringFactoryInstance()) < 0 ? asNO_MODULE
                                                                                 : 0;
}

} // namespace script
} // namespace s3d

#endif // SPECTRA_SCRIPTING