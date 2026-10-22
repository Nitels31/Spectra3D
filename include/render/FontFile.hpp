#pragma once
#include <string>
#include <vector>

namespace s3d {

// THE FONT THE ENGINE SHIPS.
//
// One file, found in the assets and, failing that, on the machine, and the
// same file for the editor and for a game: the letters of the interface of a
// game are the letters of the editor, and a project that brings a font of its
// own brings it into the same place.
//
// The candidates are all that is asked for here, in the order they are worth
// trying. Opening one is FreeType's business, and it is done by the two
// things that need the letters: the atlas of the editor and the letters of a
// game. A file that is there and cannot be opened is not a font, so the next
// one is tried - and a machine with no font at all is a machine that draws no
// letters, which is worth a line in the log and not a crash.
std::vector<std::string> fontFileCandidates();

// THE FONTS BEHIND THE FIRST ONE.
//
// The first font is the interface: JetBrains Mono, the one the engine ships.
// It has the Latin letters, the Cyrillic ones and the marks of the European
// languages, and it has nothing else - no Bengali, no Chinese, no emoji, and
// that is by design.
//
// A letter the first font does not have is taken from the next font that has
// it. These are the fonts that are asked, in the order they are worth asking:
// the ones the engine ships first, so that a language works on a machine that
// has nothing installed, and the ones of the machine after that, so that a
// machine with a script font draws the letters of that script even when the
// engine's assets are from an older build.
//
// A letter no font of the chain has is not drawn at all. It is not drawn as a
// box and not as a question mark either - an empty box in the middle of a word
// is the thing this engine must never show.
std::vector<std::string> fallbackFontCandidates();

} // namespace s3d
