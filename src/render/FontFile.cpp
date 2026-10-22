#include "render/FontFile.hpp"

#include "core/Str.hpp"
#include "platform/Filesystem.hpp"

namespace s3d {
namespace {

// The font the engine ships with, in the assets next to the program. It is
// looked for first and it is the reason the editor looks the same on every
// machine: a system font is a different font on every system, and on a machine
// with none of them the editor has no letters at all.
//
// The name is the one the file has in the assets; the second is what a build
// with an older assets folder may have.
const char* const kBundledFonts[] = {
    "fonts/JetBrainsMono-Regular.ttf",
    "fonts/JetBrainsMono.ttf",
};

// The fonts the letters of the other scripts come from.
//
// The Bengali one is shipped for the same reason the first one is: a language
// that is in the list of languages has to be readable on a machine that has
// nothing installed, and the Bengali letters are not in a font of Latin
// letters. The name is the one the file has in the assets; the second is the
// spelling a build of it may have.
const char* const kBundledFallbackFonts[] = {
    "fonts/NotoSansBengali-Variable.ttf",
    "fonts/NotoSansBengali-Regular.ttf",
    "fonts/NotoSansBengali.ttf",
};

// System fonts tried after the bundled one, which is the fallback for a build
// whose assets folder was not copied along. Windows first, then Linux, then
// macOS - the same order on every platform, so a machine with more than one of
// them still ends up with the same letters.
const char* const kSystemFontPaths[] = {
    "C:\\Windows\\Fonts\\consola.ttf",
    "C:\\Windows\\Fonts\\CascadiaMono.ttf",
    "C:\\Windows\\Fonts\\CascadiaCode.ttf",
    "C:\\Windows\\Fonts\\segoeui.ttf",
    "C:\\Windows\\Fonts\\tahoma.ttf",
    "C:\\Windows\\Fonts\\arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/TTF/NotoSans.ttf",
    "/Library/Fonts/Arial.ttf",
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/SFNSMono.ttf",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "/System/Library/Fonts/Supplemental/Courier New.ttf",
};

// The same list for the letters the first font does not have. Every one of
// them is opened and a letter is taken from the first that has it, so the
// order only decides which font wins a letter two of them both have.
//
// The Bengali ones are first: they are the letters of a language the engine
// ships, and they are the letters a font of Latin letters does not have.
const char* const kSystemFallbackFontPaths[] = {
    "C:\\Windows\\Fonts\\NirmalaB.ttf",
    "C:\\Windows\\Fonts\\Nirmala.ttf",
    "C:\\Windows\\Fonts\\seguisym.ttf",
    "C:\\Windows\\Fonts\\segoeui.ttf",
    "C:\\Windows\\Fonts\\tahoma.ttf",
    "C:\\Windows\\Fonts\\arial.ttf",
    "C:\\Windows\\Fonts\\arialuni.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansBengali-Regular.ttf",
    "/usr/share/fonts/opentype/noto/NotoSansBengali-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansDevanagari-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/TTF/NotoSansBengali-Regular.ttf",
    "/System/Library/Fonts/Supplemental/Kohinoor Bangla.ttc",
    "/System/Library/Fonts/Kohinoor Bangla.ttc",
    "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
};

} // namespace

std::vector<std::string> fontFileCandidates() {
    std::vector<std::string> candidates;

    // The project's own fonts come before the engine's: a project that brings
    // a font of its own has it baked into the letters.
    for (const std::string& root : fs::assetRoots()) {
        for (const char* name : kBundledFonts) {
            const std::string path = str::join(root, name);
            if (fs::exists(path)) candidates.push_back(path);
        }
    }
    // The system ones are the fallback: they are there on every machine that
    // has a font at all, and they are not the same font everywhere - which is
    // why the bundled one is looked for first.
    for (const char* path : kSystemFontPaths)
        if (fs::exists(path)) candidates.push_back(path);
    return candidates;
}

std::vector<std::string> fallbackFontCandidates() {
    std::vector<std::string> candidates;

    // The project's own fonts come first, for the same reason as above: a
    // project that brings a font of its own has its letters drawn with it.
    for (const std::string& root : fs::assetRoots()) {
        for (const char* name : kBundledFallbackFonts) {
            const std::string path = str::join(root, name);
            if (fs::exists(path)) candidates.push_back(path);
        }
    }
    for (const char* path : kSystemFallbackFontPaths)
        if (fs::exists(path)) candidates.push_back(path);
    return candidates;
}

} // namespace s3d
