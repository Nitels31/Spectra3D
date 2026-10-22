#include "player/Player.hpp"

#include "core/CrashLog.hpp"

#include <exception>

int main() {
    // An exception that got out of the program is a crash nobody was there to
    // see: the report says what it was, and the program stops instead of
    // ending in a dialog nobody understands.
    try {
        s3d::Player player;
        if (!player.initialize()) return 1;
        return player.run();
    } catch (const std::exception& e) {
        s3d::CrashLog::write(e.what());
        return 1;
    } catch (...) {
        s3d::CrashLog::write("an exception nobody can name got out of the program");
        return 1;
    }
}
