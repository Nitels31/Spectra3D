#include "Filesystem.hpp"

#include "../core/Log.hpp"
#include "../core/Str.hpp"

#include <algorithm>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace s3d {
namespace fs {
namespace {

std::string g_exeDir;
std::string g_assetRoot;

#if defined(_WIN32)
std::string wideToUtf8(const wchar_t* w, size_t len) {
    if (!w || !len) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, (int)len, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, (int)len, &out[0], n, nullptr, nullptr);
    return out;
}
#endif

std::string parentOf(const std::string& dir) {
    const size_t slash = dir.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : dir.substr(0, slash);
}

std::string currentDir() {
#if defined(_WIN32)
    char buffer[MAX_PATH];
    const DWORD length = GetCurrentDirectoryA(MAX_PATH, buffer);
    return length > 0 ? std::string(buffer, length) : std::string();
#else
    char buffer[4096];
    return getcwd(buffer, sizeof(buffer)) ? std::string(buffer) : std::string();
#endif
}

// Turns a possibly relative path into an absolute one. Required because the
// working directory is moved to the binary directory right after discovery.
std::string absolute(const std::string& path) {
    if (path.empty()) return path;
    if (path.size() > 1 && (path[1] == ':' || path[0] == '/')) return path;
    const std::string root = currentDir();
    return root.empty() ? path : str::join(root, path);
}

void discoverAssetRoot() {
    // Candidate roots, most specific first. Covers launching from the build
    // directory, from the project root, and from an arbitrary working dir.
    std::vector<std::string> candidates;
    if (!g_exeDir.empty()) {
        candidates.push_back(str::join(g_exeDir, "assets"));
        candidates.push_back(str::join(parentOf(g_exeDir), "assets"));
    }
#if defined(SPECTRA_ASSET_DIR)
    candidates.emplace_back(SPECTRA_ASSET_DIR);
#endif
    candidates.emplace_back("assets");
    candidates.emplace_back("../assets");

    for (const std::string& c : candidates) {
        if (!isDirectory(str::join(c, "lang"))) continue;
        g_assetRoot = absolute(c);
        break;
    }
    if (g_assetRoot.empty())
        Log::warn("Assets: no asset root found near '%s'", g_exeDir.c_str());
    else
        Log::info("Assets: %s", g_assetRoot.c_str());
}

} // namespace

const std::string& executableDir() { return g_exeDir; }

void adoptAssetWorkingDir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len > 0) {
        g_exeDir = wideToUtf8(buf, len);
        const size_t slash = g_exeDir.find_last_of("/\\");
        if (slash != std::string::npos) g_exeDir = g_exeDir.substr(0, slash);
    }
#else
    // /proc/self/exe is the portable-enough answer on Linux/macOS.
    char buf[4096];
    const ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        g_exeDir = buf;
        const size_t slash = g_exeDir.find_last_of('/');
        if (slash != std::string::npos) g_exeDir = g_exeDir.substr(0, slash);
    }
#endif

    discoverAssetRoot();

    // Write runtime output next to the binary so the source tree stays clean.
    if (!g_exeDir.empty()) {
#if defined(_WIN32)
        SetCurrentDirectoryA(g_exeDir.c_str());
#else
        if (chdir(g_exeDir.c_str()) != 0) Log::warn("chdir(%s) failed", g_exeDir.c_str());
#endif
    }
}

const std::string& assetRoot() { return g_assetRoot; }

bool isDirectory(const std::string& path) {
#if defined(_WIN32)
    const DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

bool listDirectory(const std::string& dir, std::vector<std::string>& out) {
    out.clear();
#if defined(_WIN32)
    WIN32_FIND_DATAA fd;
    const std::string pattern = str::join(dir, "*");
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        const std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;
        out.push_back(name);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(dir.empty() ? "." : dir.c_str());
    if (!d) return false;
    while (struct dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        out.push_back(name);
    }
    closedir(d);
#endif
    std::sort(out.begin(), out.end());
    return true;
}

std::vector<std::string> listFilesByExtension(
    const std::string& dir, const std::vector<std::string>& extensions) {
    std::vector<std::string> names;
    listDirectory(dir, names);

    std::vector<std::string> found;
    for (const std::string& name : names) {
        const std::string ext = str::extension(name);
        if (ext.empty()) continue;
        if (std::find(extensions.begin(), extensions.end(), ext) == extensions.end()) continue;
        if (isDirectory(str::join(dir, name))) continue;
        found.push_back(str::join(dir, name));
    }
    return found;
}


} // namespace fs
} // namespace s3d