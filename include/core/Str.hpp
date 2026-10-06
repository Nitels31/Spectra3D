#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace s3d {

// UTF-8 aware helpers shared by the renderer (glyph iteration) and the asset
// layer (file names, extensions). Kept header-only: small and stateless.
namespace str {

// Decodes one UTF-8 code point starting at `p`, advances `p` past it.
// Invalid bytes decode as '?' and consume one byte.
inline unsigned nextCodepoint(const char*& p) {
    const unsigned char b0 = (unsigned char)*p;
    if (b0 == '\0') return 0;
    if (b0 < 0x80) {
        p += 1;
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0 && p[1] != '\0') {
        const unsigned cp = ((unsigned)(b0 & 0x1F) << 6) | (unsigned)(p[1] & 0x3F);
        p += 2;
        return cp;
    }
    p += 1;
    return '?';
}

inline unsigned nextCodepoint(const char* p, const char* end) {
    const unsigned char b0 = (unsigned char)*p;
    if (b0 < 0x80) return b0;
    if ((b0 & 0xE0) == 0xC0 && p + 1 < end)
        return ((unsigned)(b0 & 0x1F) << 6) | (unsigned)(p[1] & 0x3F);
    return '?';
}

inline char lowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

inline std::string toLowerAscii(std::string s) {
    for (char& c : s) c = lowerAscii(c);
    return s;
}

inline std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// --- Non-owning path views -------------------------------------------------
// The `*View` twins below hand back a slice of the caller's buffer instead of a
// heap copy. They are what the asset layer should use: probing a directory for
// extensions or stems happens once per candidate file and used to allocate two
// std::strings per probe. The `std::string` wrappers further down are defined in
// terms of these, so the two can never disagree.

inline std::string_view fileNameView(std::string_view path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

inline std::string_view fileStemView(std::string_view path) {
    const std::string_view name = fileNameView(path);
    const size_t dot = name.find_last_of('.');
    return dot == std::string_view::npos ? name : name.substr(0, dot);
}

// Extension including the dot, empty when there is none. Case is preserved so no
// copy is needed; compare it with equalsIgnoreAscii() below.
inline std::string_view extensionView(std::string_view path) {
    const std::string_view name = fileNameView(path);
    const size_t dot = name.find_last_of('.');
    return dot == std::string_view::npos ? std::string_view() : name.substr(dot);
}

// ASCII case-insensitive equality, used in place of "lowercase into a temporary
// then compare". Both sides are folded: an earlier version folded only `a` and
// demanded a lowercase literal on the right, which silently broke every caller
// that passed real text - a Windows path like "C:/Games/MyProject" never matched
// itself, so the recent list grew a duplicate per launch.
inline bool equalsIgnoreAscii(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (lowerAscii(a[i]) != lowerAscii(b[i])) return false;
    return true;
}

// Last path component, both separators accepted.
inline std::string fileName(const std::string& path) {
    return std::string(fileNameView(path));
}

// File name without directory or extension.
inline std::string fileStem(const std::string& path) {
    return std::string(fileStemView(path));
}

// Lowercase extension including the dot, or "" when there is none.
inline std::string extension(const std::string& path) {
    std::string ext(extensionView(path));
    for (char& c : ext) c = lowerAscii(c);
    return ext;
}

inline std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    const char last = a[a.size() - 1];
    if (last == '/' || last == '\\') return a + b;
    return a + "/" + b;
}

} // namespace str
} // namespace s3d