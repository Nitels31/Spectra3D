#include "Log.hpp"

#include <cstdio>
#include <cstring>

namespace s3d {
namespace {

constexpr size_t kMaxEntries = 256;
constexpr size_t kTrimTo = 128;

std::vector<Log::Entry> g_entries;
std::string g_dir;
FILE* g_file = nullptr;

const char* levelTag(Log::Level l) {
    switch (l) {
        case Log::Level::Warn: return "WARN";
        case Log::Level::Error: return "ERR!";
        case Log::Level::Info:
        default: return "INFO";
    }
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
}

const char* Log::fileDirectory() { return g_dir.c_str(); }

void Log::write(Level level, const char* fmt, va_list args) {
    Entry e;
    e.level = level;
    vsnprintf(e.text, sizeof(e.text), fmt, args);
    g_entries.push_back(e);
    if (g_entries.size() > kMaxEntries)
        g_entries.erase(g_entries.begin(), g_entries.begin() + (g_entries.size() - kTrimTo));

    if (g_file) {
        fprintf(g_file, "[%s] %s\n", levelTag(level), e.text);
        fflush(g_file);
    }
    fprintf(stderr, "[%s] %s\n", levelTag(level), e.text);
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

void Log::clear() { g_entries.clear(); }

} // namespace s3d