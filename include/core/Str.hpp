#pragma once
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

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

// Encodes one code point as UTF-8 and returns how many bytes it took.
//
// The engine's text is UTF-8 everywhere - the labels, the file names, the
// scene files - and a text field that holds one byte per character cannot
// hold a Ukrainian letter: it would cut it in half.
inline int utf8Encode(unsigned cp, char* out) {
    if (cp < 0x80u) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800u) {
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp < 0x10000u) {
        out[0] = (char)(0xE0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        return 3;
    }
    out[0] = (char)(0xF0u | (cp >> 18));
    out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (cp & 0x3Fu));
    return 4;
}

// The length of the text with its LAST CHARACTER taken off.
//
// The last character and not the last byte: a backspace that takes half a
// letter off leaves a letter that cannot be drawn and a field that cannot be
// typed into again.
inline int utf8WithoutLast(const char* text, int length) {
    if (length <= 0) return 0;
    int at = length - 1;
    // The continuation bytes of a multi-byte character all start with 0x80.
    while (at > 0 && ((unsigned char)text[at] & 0xC0u) == 0x80u) --at;
    return at;
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
    if (last == '/' || last == '\\') return a + "/" + b;
    return a + "/" + b;
}

// BASE64: BINARY IN A TEXT FILE.
//
// A heightfield is thousands of numbers, and a scene file is a file a person
// reads. The numbers go into the file as one line of letters instead of as
// thousands of lines of numbers: the file stays readable, and the ground it
// describes is still the ground.
//
// The alphabet is the usual one, and the padding is the usual '='.
inline std::string toBase64(const void* data, size_t bytes) {
    static const char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const unsigned char* in = (const unsigned char*)data;
    std::string out;
    out.reserve((bytes + 2) / 3 * 4);

    size_t at = 0;
    while (at + 3 <= bytes) {
        const unsigned value = ((unsigned)in[at] << 16) | ((unsigned)in[at + 1] << 8) |
                               (unsigned)in[at + 2];
        out += kAlphabet[(value >> 18) & 0x3Fu];
        out += kAlphabet[(value >> 12) & 0x3Fu];
        out += kAlphabet[(value >> 6) & 0x3Fu];
        out += kAlphabet[value & 0x3Fu];
        at += 3;
    }
    if (at < bytes) {
        const unsigned value = (unsigned)in[at] << 16;
        out += kAlphabet[(value >> 18) & 0x3Fu];
        out += kAlphabet[(value >> 12) & 0x3Fu];
        out += (at + 1 < bytes) ? kAlphabet[(value >> 6) & 0x3Fu] : '=';
        out += '=';
    }
    return out;
}

// The other direction. False when the text is not base64: a file that was
// written by something else is a file that is not read, and saying so is
// better than reading it as noise.
inline bool fromBase64(const std::string& text, std::vector<unsigned char>& out) {
    // What every letter of the alphabet stands for. -1 is "not a letter".
    //
    // Built once, when the first thing is decoded: the table is the same for
    // every call, and building it again for every ground in a scene would be
    // work nobody asked for.
    static const std::array<int, 256> table = [] {
        std::array<int, 256> letters;
        letters.fill(-1);
        const char* alphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) letters[(unsigned char)alphabet[i]] = i;
        return letters;
    }();

    out.clear();
    out.reserve(text.size() / 4 * 3);
    unsigned value = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=' || c == '\n' || c == '\r') continue;
        const int letter = table[(unsigned char)c];
        if (letter < 0) return false;
        value = (value << 6) | (unsigned)letter;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((unsigned char)((value >> bits) & 0xFFu));
        }
    }
    return true;
}

} // namespace str
} // namespace s3d
