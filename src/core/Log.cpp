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
    const std::string next = dir ? dir : "";
    // The same folder again: the log is already open there, and opening it a
    // second time would truncate the file and throw away everything that was
    // written into it. Two programs set the folder - the editor and the
    // player - and both of them say something before the window is open.
    if (next == g_dir && g_file) return;

    g_dir = next;
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
        // The file is shared, and the ring buffer is read by the crash handler
        // from any thread, so the two are under the same lock.
        std::lock_guard<std::mutex> lock(g_mutex);
        if (isOwner()) {
            g_entries.push_back(e);
            trim();
        } else {
            // Another thread: the entry waits for the owner to take it.
            g_pending.push_back(e);
        }
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
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (const Entry& e : pending) {
            g_entries.push_back(e);
            trim();
        }
    }
}

size_t Log::copyEntries(char* out, size_t capacity) {
    if (!out || capacity == 0) return 0;
    out[0] = '\0';

    // The lock is taken with try_lock and not with lock: the caller is a
    // crash handler, and it must not wait for a thread that is busy logging
    // and is never going to let go.
    std::unique_lock<std::mutex> lock(g_mutex, std::try_to_lock);
    if (!lock.owns_lock()) return 0;

    const size_t budget = capacity - 1;
    size_t used = 0;

    // The size of a message as it is written here: "[TAG] text\n".
    auto sizeOf = [](const Entry& e) {
        return 4 + std::strlen(levelTag(e.level)) + std::strlen(e.text);
    };

    // One message, put into the buffer. False when it does not fit.
    auto append = [&](const Entry& e) {
        const char* const tag = levelTag(e.level);
        const size_t tagLength = std::strlen(tag);
        size_t textLength = std::strlen(e.text);
        if (textLength > sizeof(e.text) - 1) textLength = sizeof(e.text) - 1;
        const size_t need = 4 + tagLength + textLength;
        if (used + need > budget) return false;
        char* p = out + used;
        p[0] = '[';
        std::memcpy(p + 1, tag, tagLength);
        p[1 + tagLength] = ']';
        p[2 + tagLength] = ' ';
        std::memcpy(p + 3 + tagLength, e.text, textLength);
        p[3 + tagLength + textLength] = '\n';
        used += need;
        return true;
    };

    // The ring buffer first: it is the older of the two. When not everything
    // fits, the oldest messages are the ones that go - the last thing the
    // program said is the thing that matters.
    size_t entriesSize = 0;
    for (const Entry& e : g_entries) entriesSize += sizeOf(e);
    size_t start = 0;
    while (entriesSize > budget && start < g_entries.size()) {
        entriesSize -= sizeOf(g_entries[start]);
        ++start;
    }
    for (size_t i = start; i < g_entries.size(); ++i)
        if (!append(g_entries[i])) break;

    // The messages of the other threads: the newest ones, and the ones nobody
    // took yet.
    size_t pendingSize = 0;
    for (const Entry& e : g_pending) pendingSize += sizeOf(e);
    size_t pendingStart = 0;
    while (used + pendingSize > budget && pendingStart < g_pending.size()) {
        pendingSize -= sizeOf(g_pending[pendingStart]);
        ++pendingStart;
    }
    for (size_t i = pendingStart; i < g_pending.size(); ++i)
        if (!append(g_pending[i])) break;

    out[used] = '\0';
    return used;
}

void Log::clear() { g_entries.clear(); }

} // namespace s3d