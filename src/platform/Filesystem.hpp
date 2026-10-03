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

// Makes the process working directory point at `exeDir/assets`, so runtime
// files (logs, scenes) are written next to the binary instead of wherever the
// user happened to launch it from.
void adoptAssetWorkingDir();

// Absolute directory that holds the bundled assets. Empty when not found.
const std::string& assetRoot();

bool isDirectory(const std::string& path);

// Entries of `dir` (no "." / ".."), sorted. Returns false when unreadable.
bool listDirectory(const std::string& dir, std::vector<std::string>& out);

// Files in `dir` whose extension is one of `extensions` (lowercase, with dot).
// Names are prefixed with the relative `dir` so they can be opened directly.
std::vector<std::string> listFilesByExtension(const std::string& dir,
                                             const std::vector<std::string>& extensions);


} // namespace fs
} // namespace s3d