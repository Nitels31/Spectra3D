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

} // namespace s3d
