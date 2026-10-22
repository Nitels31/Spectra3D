#pragma once
#include <string>

#include "app/GameHost.hpp"

namespace s3d {

// The game as a program.
//
// There is no launcher here and no editor: the program opens the boot file
// beside it, loads the world that file names and runs the game. That is the
// whole of it - the engine, a window and a loop - and it is the program a
// build of a game is made of.
//
// The editor and this program are built from the same game (a GameHost), so
// the game a player plays is the game the editor edits.
class Player {
public:
    // Opens the window and the game. False when there is no game to play: the
    // program is beside a folder of data with a boot file in it, and without
    // that file it has nothing to open.
    bool initialize();

    // The loop, until the window is closed.
    int run();

private:
    GameHost game_;
};

} // namespace s3d
