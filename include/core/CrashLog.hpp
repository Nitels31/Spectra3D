#pragma once
#include <string>

namespace s3d {

// The account of a crash.
//
// A crash is the one failure the engine cannot log its way out of: the log
// lives in a buffer, and the buffer dies with the process. So the report is
// written by the handler itself, straight to the disk, and it carries the last
// words of the log with it - the ring buffer is still in memory when the
// handler runs, and it is the only account of what the program was doing.
//
// The handlers are installed once, as early as the program can: the platform's
// own for a hard fault, the C standard ones for the signals, and the terminate
// handler for an exception that got out of the program.
//
// Like the log, this is process-wide and static: the handler that runs when
// the program dies cannot be handed an instance of anything.
class CrashLog {
public:
    // Installs the handlers. Calling it again does nothing.
    static void install();

    // The name of the program in the report: the engine's own name for the
    // editor, the name of the game for the player.
    static void setProgramName(const char* name);

    // The folder the report is written into. Empty means the working
    // directory. The path is in the machine's own code page, like the log's.
    static void setDirectory(const char* dir);
    static const char* directory();

    // Writes a report without killing the program, for a failure the program
    // survived and wants on the record. Returns the file it wrote, or an
    // empty string when it could not write one.
    static std::string write(const char* what);
};

} // namespace s3d
