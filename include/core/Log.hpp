#pragma once
#include <cstdarg>
#include <string>
#include <vector>

namespace s3d {

// Process-wide diagnostic sink: stderr + `log/engine.log` + in-memory ring buffer
// consumed by the editor console panel. Intentionally a singleton - every layer
// needs logging and threading an instance through call stacks buys nothing.
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

private:
    static void write(Level level, const char* fmt, va_list args);
};

} // namespace s3d