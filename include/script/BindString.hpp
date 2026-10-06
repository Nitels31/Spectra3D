#pragma once

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>

namespace s3d {
namespace script {

// The script-visible `string`.
//
// AngelScript has no built-in string type: it belongs to the application, which
// registers both an object type and an asIStringFactory for the literals. The
// vendored SDK ships neither the add-on nor a default, so this is the whole
// implementation. BindString.cpp registers it before anything else.
//
// Bindings take `const ScriptString&`, never `const std::string&`. The two are
// one pointer apart at runtime, but the engine only ever hands back a pointer to
// one of these, so naming the real type keeps that from being a silent type pun.
//
// The characters live in a heap buffer rather than in a std::string member
// because AngelScript can allocate an object of a registered type with plain
// malloc (asCObjectType copy path) and run a constructor over that raw memory.
// Keeping the struct trivially constructible means that path, if it is ever
// taken, cannot leave a std::string uninitialised.
//
// Lifetime is reference counted because the engine attaches a release clean-up to
// every `const string &in` argument. String literals are the exception: the
// compiler pushes their pointer without ever taking a reference and promises the
// factory will keep them valid for the whole module, so `constant` marks them and
// the factory alone decides when they die.
struct ScriptString {
    // --- construction ---
    // Resizes so at least `needed` bytes fit, always leaving room for the
    // terminating NUL. Returns false when the allocation fails, in which case
    // the string is left exactly as it was rather than half written.
    bool reserve(unsigned int needed) {
        if (capacity >= needed) return true;

        unsigned int grown = capacity ? capacity : 16;
        while (grown < needed) grown *= 2;

        char* memory = static_cast<char*>(realloc(buffer, grown));
        if (!memory) return false;
        buffer = memory;
        capacity = grown;
        return true;
    }

    void create(const char* data, unsigned int length) {
        destroy();
        if (!reserve(length + 1)) return;
        if (data && length) memcpy(buffer, data, length);
        buffer[length] = '\0';
        size = length;
    }

    void createEmpty() { create(nullptr, 0); }

    void destroy() {
        free(buffer);
        buffer = nullptr;
        size = 0;
        capacity = 0;
    }

    // --- lifetime ---
    void addRef() {
        if (!constant) ++references;
    }
    // Drops one reference and reports whether the last one just went away. The
    // string factory reuses the same rule when it disposes of a literal.
    bool release() { return --references <= 0; }
    void releaseSelf() {
        if (constant || !release()) return;
        destroy();
        delete this;
    }

    // --- access ---
    const char* c_str() const { return buffer ? buffer : ""; }
    unsigned int length() const { return size; }
    // For the bindings that hand the text to something expecting std::string.
    std::string str() const { return std::string(buffer, size); }

    // --- behaviour ---
    ScriptString& set(const ScriptString& in);
    // A reference type cannot be returned by value, so concatenation hands back
    // a reference to a fresh object that the engine releases.
    ScriptString& opAdd(const ScriptString& in) const;
    int opCmp(const ScriptString& in) const;
    bool opEquals(const ScriptString& in) const;
    void setLength(unsigned int wanted);

    char* buffer = nullptr;
    unsigned int size = 0;
    unsigned int capacity = 0;
    int references = 1;
    // Set only on objects handed out by asIStringFactory::GetStringConstant.
    // Those are owned by the factory for the whole module, so addref/release on
    // them are no-ops and only ReleaseStringConstant frees them.
    bool constant = false;
};

} // namespace script
} // namespace s3d