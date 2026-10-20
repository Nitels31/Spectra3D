#pragma once
#include <cstdarg>
#include <string>
#include <vector>

namespace s3d {

// Process-wide diagnostic sink: stderr + `log/engine.log` + in-memory ring buffer
// consumed by the editor console panel. Intentionally a singleton - every layer
// needs logging and threading an instance through call stacks buys nothing.
//
// The loader threads write here too, so the sink is safe to write from more than
// one thread: the file is written under a lock, and a message from another
// thread waits in a queue until `drain()` takes it into the ring buffer. The
// ring buffer belongs to the thread that reads it - the editor's console panel
// reads it every frame - so nothing else touches it.
class Log {
public:
    enum class Level { Info = 0, Warn = 1, Error = 2 };

    struct Entry {
        Level level;
        char text[192];
    };

    static void info(const char* fmt, ...);
    static void warn(const char* fmt, ...);
    static void error(const char* fmt, ...);

    // Directory the log file lives in. Defaults to the process working directory.
    static void setFileDirectory(const char* dir);
    static const char* fileDirectory();

    static const std::vector<Entry>& entries();
    static void clear();

    // Takes the messages the other threads wrote into the ring buffer. Called
    // by the thread that owns the buffer, once per frame.
    static void drain();

private:
    static void write(Level level, const char* fmt, va_list args);
};

} // namespace s3d