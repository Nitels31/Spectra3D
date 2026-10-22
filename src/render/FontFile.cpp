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

} // namespace s3d
