#pragma once
#include <string>

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

// Last path component, both separators accepted.
inline std::string fileName(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// File name without directory or extension.
inline std::string fileStem(const std::string& path) {
    std::string name = fileName(path);
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

// Lowercase extension including the dot, or "" when there is none.
inline std::string extension(const std::string& path) {
    const std::string name = fileName(path);
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? std::string() : toLowerAscii(name.substr(dot));
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