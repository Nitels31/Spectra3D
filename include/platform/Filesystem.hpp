#pragma once
#include <string>
#include <vector>

namespace s3d {

// Thin, portable filesystem facade. Directory listing and process paths are the
// only OS-specific things the engine needs, so they live here instead of being
// re-implemented with #ifdefs at every call site.
namespace fs {

// Absolute directory containing the running executable ("" when unknown).
const std::string& executableDir();

// Full path of the running executable itself. "Build" stages this file, so it has
// to be the real one and not a name guessed from the directory.
const std::string& executablePath();


// Makes the process working directory point at `exeDir/assets`, so runtime
// files (logs, scenes) are written next to the binary instead of wherever the
// user happened to launch it from.
void adoptAssetWorkingDir();

// Absolute directory that holds the bundled assets. Empty when not found.
const std::string& assetRoot();

bool isDirectory(const std::string& path);
bool exists(const std::string& path);

// One entry as reported by the OS listing, including whether it is a directory.
struct DirEntry {
    std::string name;
    bool isDir = false;
};

// Entries of `dir` (no "." / ".."), sorted by name. Returns false when
// unreadable. The directory flag comes from the listing itself, so callers that
// need it do not have to stat every candidate.
bool listDirectoryEx(const std::string& dir, std::vector<DirEntry>& out);

// Entries of `dir` (no "." / ".."), sorted. Returns false when unreadable.
bool listDirectory(const std::string& dir, std::vector<std::string>& out);

// Files in `dir` whose extension is one of `extensions` (lowercase, with dot).
// Names are prefixed with the relative `dir` so they can be opened directly.
std::vector<std::string> listFilesByExtension(const std::string& dir,
                                             const std::vector<std::string>& extensions);

// Whole-file IO. Returns false when the file cannot be opened.
bool readFile(const std::string& path, std::string& out);
// Text mode, so line endings are native to the platform.
bool writeFile(const std::string& path, const std::string& contents);

// Copies `src` (a file or a whole directory) to `dst`, creating directories as
// needed and overwriting what is there. The engine's "Build" is exactly this: the
// player IS this executable, so a build is the binary plus its runtime files plus
// the project, laid out somewhere the user can ship.
bool copyTree(const std::string& src, const std::string& dst);

// Deletes `path` and, when it is a directory, everything inside it, depth first.
// Returns false when something could not be removed. Read-only files and
// directories are made writable first: a project folder that came from a zip or
// a git checkout must still be deletable.
bool removeTree(const std::string& path);

// Outcome of pickFolder(). The distinction between "the user pressed Cancel" and
// "this platform has no native dialog" matters: the launcher only falls back to
// its own browser for the second one.
enum class PickResult { Picked, Cancelled, Unsupported };

// Opens the platform's own folder chooser - the Windows Explorer shell dialog -
// starting at `startPath`. On success `out` receives the chosen absolute path.
//
// This is the real shell dialog on purpose. A hand-rolled in-app browser looks
// like the system one until the user tries a network share, a hidden folder or a
// typed UNC path, none of which an in-app listing can offer.
PickResult pickFolder(const std::string& startPath, std::string& out);

} // namespace fs
} // namespace s3d