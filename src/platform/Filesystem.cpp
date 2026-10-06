#include "platform/Filesystem.hpp"

#include "core/Log.hpp"
#include "core/Str.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// The shell folder dialog. shobjidl(_core) covers IFileDialog and IShellItem;
// objbase covers COM itself. MinGW ships the pre-C++11 header name.
#if defined(__MINGW32__)
#include <shobjidl.h>
#else
#include <shobjidl_core.h>
#endif
#include <objbase.h>
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
// The module path as the OS reported it, before the directory was cut off it.
std::string g_exePath;

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

const std::string& executablePath() { return g_exePath; }

void adoptAssetWorkingDir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len > 0) {
        g_exePath = wideToUtf8(buf, len);
        g_exeDir = g_exePath;
        const size_t slash = g_exeDir.find_last_of("/\\");
        if (slash != std::string::npos) g_exeDir = g_exeDir.substr(0, slash);
    }
#else
    // /proc/self/exe is the portable-enough answer on Linux/macOS.
    char buf[4096];
    const ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        g_exePath = buf;
        g_exeDir = g_exePath;
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

bool exists(const std::string& path) {
#if defined(_WIN32)
    return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0;
#endif
}

bool isDirectory(const std::string& path) {
#if defined(_WIN32)
    const DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

bool listDirectoryEx(const std::string& dir, std::vector<DirEntry>& out) {
    out.clear();
#if defined(_WIN32)
    WIN32_FIND_DATAA fd;
    const std::string pattern = str::join(dir, "*");
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        const std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;
        // dwFileAttributes is part of the listing, so no GetFileAttributesA per
        // entry is needed.
        out.push_back(DirEntry{name, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0});
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(dir.empty() ? "." : dir.c_str());
    if (!d) return false;
    while (struct dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        bool isDir = false;
        // d_type is free on Linux/macOS; DT_LNK is deliberately treated as
        // unknown because the old stat()-based check followed symlinks.
#if defined(DT_DIR)
        if (e->d_type == DT_DIR) {
            isDir = true;
        } else if (e->d_type == DT_REG) {
            isDir = false;
        } else {
            struct stat st;
            const std::string full =
                dir.empty() ? name : (dir.back() == '/' ? dir + name : dir + "/" + name);
            isDir = stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
        }
#else
        struct stat st;
        const std::string full =
            dir.empty() ? name : (dir.back() == '/' ? dir + name : dir + "/" + name);
        isDir = stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
        out.push_back(DirEntry{name, isDir});
    }
    closedir(d);
#endif
    std::sort(out.begin(), out.end(),
              [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });
    return true;
}

bool listDirectory(const std::string& dir, std::vector<std::string>& out) {
    std::vector<DirEntry> entries;
    if (!listDirectoryEx(dir, entries)) {
        out.clear();
        return false;
    }
    out.clear();
    out.reserve(entries.size());
    for (const DirEntry& entry : entries) out.push_back(entry.name);
    return true;
}

std::vector<std::string> listFilesByExtension(
    const std::string& dir, const std::vector<std::string>& extensions) {
    // The listing already knows which entries are directories, and the extension
    // can be tested as a view, so this loop makes no syscalls and no allocations
    // beyond the returned paths.
    std::vector<DirEntry> entries;
    listDirectoryEx(dir, entries);

    std::vector<std::string> found;
    for (const DirEntry& entry : entries) {
        const std::string_view ext = str::extensionView(entry.name);
        if (ext.empty()) continue;
        bool listed = false;
        for (const std::string& candidate : extensions) {
            if (!str::equalsIgnoreAscii(ext, candidate)) continue;
            listed = true;
            break;
        }
        if (!listed) continue;
        if (entry.isDir) continue;
        found.push_back(str::join(dir, entry.name));
    }
    return found;
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    out.swap(text);
    return true;
}

bool writeFile(const std::string& path, const std::string& contents) {
    std::ofstream file(path); // text mode: CRLF on Windows, LF elsewhere
    if (!file) return false;
    file.write(contents.data(), (std::streamsize)contents.size());
    file.flush();
    return (bool)file;
}

namespace {

// Creates one directory level. Every level is created by hand because the OS calls
// differ in whether they create the whole path or only the last component: the
// Windows one creates only the leaf, and on a path that does not exist yet it
// fails. A build target is normally asked for as "project/build", where "project"
// exists - but a typed path does not have to.
void makeDirectory(const std::string& path) {
#if defined(_WIN32)
    CreateDirectoryA(path.c_str(), nullptr);
#else
    mkdir(path.c_str(), 0777);
#endif
}

// Every directory above `path`, created from the top down. Files cannot be written
// into a folder that is not there, and a copy has to be able to reach the leaf
// whether or not the user spelled out the path to it.
void makeParents(const std::string& path) {
    for (size_t slash = path.find_first_of("/\\"); slash != std::string::npos;
         slash = path.find_first_of("/\\", slash + 1)) {
        const std::string prefix = path.substr(0, slash);
        if (prefix.empty() || isDirectory(prefix)) continue;
        makeParents(prefix);
        makeDirectory(prefix);
    }
}

// One level of the copy: a file, or a directory and everything under it.
bool copyEntry(const std::string& src, const std::string& dst, bool isDir) {
    if (isDir && !isDirectory(dst)) {
        makeParents(dst);
        makeDirectory(dst);
        if (!isDirectory(dst)) return false;
    }
    if (!isDir) {
        // The destination folder has to exist even when src is a single file: a
        // build target that has never been written to is not there yet.
        makeParents(dst);
#if defined(_WIN32)
        if (CopyFileA(src.c_str(), dst.c_str(), FALSE) != 0) return true;
        // A file already open for writing cannot be overwritten in place. It is
        // copied aside and the original is replaced, which is what a rebuild of a
        // folder that holds the running program needs.
        const std::string aside = dst + ".new";
        if (CopyFileA(src.c_str(), aside.c_str(), FALSE) == 0) return false;
        if (!MoveFileExA(aside.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileA(aside.c_str());
            return false;
        }
        return true;
#else
        std::ifstream in(src, std::ios::binary);
        if (!in) return false;
        std::ofstream outFile(dst, std::ios::binary);
        if (!outFile) return false;
        outFile << in.rdbuf();
        return (bool)outFile;
#endif
    }
    std::vector<DirEntry> entries;
    if (!listDirectoryEx(src, entries)) return false;
    bool ok = true;
    for (const DirEntry& entry : entries)
        ok = copyEntry(str::join(src, entry.name), str::join(dst, entry.name), entry.isDir) && ok;
    return ok;
}

} // namespace

bool copyTree(const std::string& src, const std::string& dst) {
    if (src.empty() || dst.empty() || !exists(src)) return false;
    return copyEntry(src, dst, isDirectory(src));
}

namespace {

// One level of the delete. Kept separate from removeTree() so the recursion
// reads as "children first, then this entry", which is the only order the
// platform calls accept.
bool removeEntry(const std::string& path, bool isDir) {
    if (!isDir) {
#if defined(_WIN32)
        // A read-only file cannot be deleted on Windows, and DeleteFile fails
        // instead of clearing the attribute for us.
        const DWORD attr = GetFileAttributesA(path.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY))
            SetFileAttributesA(path.c_str(), attr & ~FILE_ATTRIBUTE_READONLY);
        return DeleteFileA(path.c_str()) != 0;
#else
        return std::remove(path.c_str()) == 0;
#endif
    }
#if defined(_WIN32)
    const DWORD attr = GetFileAttributesA(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY))
        SetFileAttributesA(path.c_str(), attr & ~FILE_ATTRIBUTE_READONLY);
    return RemoveDirectoryA(path.c_str()) != 0;
#else
    return rmdir(path.c_str()) == 0;
#endif
}

} // namespace

bool removeTree(const std::string& path) {
    if (path.empty()) return false;

    const bool dir = isDirectory(path);
    bool ok = true;
    if (dir) {
        std::vector<DirEntry> entries;
        if (!listDirectoryEx(path, entries)) {
            Log::warn("fs: cannot list '%s' while deleting it", path.c_str());
            return false;
        }
        // Depth first: every child has to be gone before the parent will accept
        // a delete, and a child that fails must not be papered over by reporting
        // the parent as removed.
        for (const DirEntry& entry : entries) {
            const std::string child =
                path.back() == '/' || path.back() == '\\' ? path + entry.name : path + "/" + entry.name;
            if (!removeTree(child)) ok = false;
        }
    }
    if (!removeEntry(path, dir)) {
        Log::error("fs: cannot remove '%s'", path.c_str());
        return false;
    }
    return ok;
}

#if defined(_WIN32)
namespace {

std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &out[0], n);
    return out;
}

} // namespace
#endif

PickResult pickFolder(const std::string& startPath, std::string& out) {
#if defined(_WIN32)
    // The shell dialog is a COM object. Initialising COM here, for the duration
    // of the call, keeps the rest of the engine free of COM - it has no other
    // use for it. S_FALSE means somebody already did it on this thread, which is
    // fine; a changed apartment model is not, and is reported as "unsupported" so
    // the caller can fall back instead of showing nothing at all.
    const HRESULT coInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(coInit) && coInit != RPC_E_CHANGED_MODE) {
        Log::error("fs: COM init failed (0x%08lx), no folder dialog", (unsigned long)coInit);
        return PickResult::Unsupported;
    }
    const bool uninitCom = SUCCEEDED(coInit);

    // IFileDialog lives in shobjidl; the wide API is the only one that handles
    // long and non-ASCII paths correctly.
    IFileDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IFileDialog, (void**)&dialog);
    if (FAILED(hr) || !dialog) {
        if (uninitCom) CoUninitialize();
        Log::error("fs: cannot create the shell folder dialog (0x%08lx)", (unsigned long)hr);
        return PickResult::Unsupported;
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);

    const std::string start = isDirectory(startPath) ? startPath : std::string();
    if (!start.empty()) {
        IShellItem* folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(utf8ToWide(start).c_str(), nullptr,
                                                  IID_IShellItem, (void**)&folder)) &&
            folder) {
            dialog->SetFolder(folder);
            folder->Release();
        }
    }

    hr = dialog->Show(nullptr);
    PickResult result = PickResult::Cancelled;
    if (SUCCEEDED(hr)) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item) {
            PWSTR raw = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)) && raw) {
                out = wideToUtf8(raw, wcslen(raw));
                CoTaskMemFree(raw);
                result = out.empty() ? PickResult::Cancelled : PickResult::Picked;
            }
            item->Release();
        }
    } else if (hr != HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        // 0x800704C7: the user pressed Cancel. That is an answer, not a failure,
        // and it must not be logged as one - ERROR_CANCELLED is a Win32 code, and
        // comparing it to the HRESULT as a bare number never matches.
        Log::warn("fs: folder dialog failed (0x%08lx)", (unsigned long)hr);
    }

    dialog->Release();
    if (uninitCom) CoUninitialize();
    return result;
#else
    // No GTK/Qt dependency is worth adding for this, so the caller falls back to
    // its own browser.
    (void)startPath;
    (void)out;
    return PickResult::Unsupported;
#endif
}

} // namespace fs
} // namespace s3d