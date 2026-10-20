#include "core/Log.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

namespace s3d {
namespace {

constexpr size_t kMaxEntries = 256;
constexpr size_t kTrimTo = 128;

std::vector<Log::Entry> g_entries;
std::string g_dir;
FILE* g_file = nullptr;

// The loader threads write here too, so the file is written under a lock and a
// message from another thread waits in a queue: the ring buffer belongs to the
// thread that reads it, and the editor's console panel reads it every frame.
std::mutex g_mutex;
std::vector<Log::Entry> g_pending;
std::thread::id g_owner;

// The thread that first logged anything owns the ring buffer.
bool isOwner() {
    if (g_owner == std::thread::id()) {
        g_owner = std::this_thread::get_id();
        return true;
    }
    return g_owner == std::this_thread::get_id();
}

void trim() {
    if (g_entries.size() > kMaxEntries)
        g_entries.erase(g_entries.begin(), g_entries.begin() + (g_entries.size() - kTrimTo));
}

const char* levelTag(Log::Level l) {
    switch (l) {
        case Log::Level::Warn: return "WARN";
        case Log::Level::Error: return "ERR!";
        case Log::Level::Info:
        default: return "INFO";
    }
}

// Registered with atexit so the buffered tail still reaches disk on a normal
// exit; warn/error are flushed eagerly as well.
void flushFile() {
    if (g_file) fflush(g_file);
}

void installExitFlush() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    std::atexit(flushFile);
}

} // namespace

void Log::setFileDirectory(const char* dir) {
    g_dir = dir ? dir : "";
    if (g_file) {
        fclose(g_file);
        g_file = nullptr;
    }
    if (g_dir.empty()) return;
    g_file = fopen((g_dir + "/engine.log").c_str(), "w");
    // One 64 KiB buffer instead of a flush (and therefore a write syscall) per
    // message. Warnings and errors are flushed immediately, so only routine
    // informational lines can sit in the buffer.
    if (g_file) setvbuf(g_file, nullptr, _IOFBF, 1 << 16);
    installExitFlush();
}

const char* Log::fileDirectory() { return g_dir.c_str(); }

void Log::write(Level level, const char* fmt, va_list args) {
    Entry e;
    e.level = level;
    vsnprintf(e.text, sizeof(e.text), fmt, args);
    if (isOwner()) {
        g_entries.push_back(e);
        trim();
    } else {
        // Another thread: the entry waits for the owner to take it.
        std::lock_guard<std::mutex> lock(g_mutex);
        g_pending.push_back(e);
    }

    // Render "[TAG] text\n" once and hand the same bytes to both sinks; the
    // previous code formatted the same line twice through stdio. vsnprintf
    // always NUL terminates, so this is exactly what "%s" used to print.
    size_t textLength = std::strlen(e.text);
    if (textLength > sizeof(e.text) - 1) textLength = sizeof(e.text) - 1;

    const char* const tag = levelTag(level);
    const size_t tagLength = std::strlen(tag);
    char line[sizeof(e.text) + 16];
    line[0] = '[';
    std::memcpy(line + 1, tag, tagLength);
    line[1 + tagLength] = ']';
    line[2 + tagLength] = ' ';
    std::memcpy(line + 3 + tagLength, e.text, textLength);
    const size_t lineLength = 3 + tagLength + textLength + 1;
    line[lineLength - 1] = '\n';

    {
        // The file is shared, so its writes are locked.
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_file) {
            fwrite(line, 1, lineLength, g_file);
            if (level != Level::Info) fflush(g_file);
        }
    }
    // stderr stays unbuffered, so every message still reaches the console as it
    // is logged.
    fwrite(line, 1, lineLength, stderr);
}

void Log::info(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    write(Level::Info, fmt, a);
    va_end(a);
}

void Log::warn(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    write(Level::Warn, fmt, a);
    va_end(a);
}

void Log::error(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    write(Level::Error, fmt, a);
    va_end(a);
}

const std::vector<Log::Entry>& Log::entries() { return g_entries; }

void Log::drain() {
    // The messages the other threads wrote, in the order they were written.
    std::vector<Entry> pending;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        pending.swap(g_pending);
    }
    for (const Entry& e : pending) {
        g_entries.push_back(e);
        trim();
    }
}

void Log::clear() { g_entries.clear(); }

} // namespace s3d