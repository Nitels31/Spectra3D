#include "core/Project.hpp"

#include "core/Log.hpp"
#include "core/Str.hpp"
#include "platform/Filesystem.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iterator>

namespace s3d {
namespace {

// Manifest schema version this build writes. Reading a higher number is allowed
// (the fields are all optional) but is reported, because it usually means the
// file was written by a build that added keys.
constexpr int kManifestVersion = 1;

// Camera defaults. Position matches Camera's own default so a freshly created
// project opens looking at the origin instead of from inside the floor.
constexpr float kDefaultCameraX = 0.0f;
constexpr float kDefaultCameraY = 6.0f;
constexpr float kDefaultCameraZ = 34.0f;

// Accepted value ranges. Out-of-range numbers in a hand-edited manifest are
// clamped instead of trusted: a 900 degree field of view would otherwise reach
// the integrator's projection matrix unchanged.
constexpr int kGravityMin = 0, kGravityMax = 5;
constexpr float kPitchLimit = 89.0f;
constexpr float kFovMin = 10.0f, kFovMax = 150.0f;

// Creates one directory level. Success is "it exists now", so a pre-created
// scripts/ folder is not an error.
//
// The engine's own file system, not the C library's mkdir: it makes every
// level that is missing, and it reads the path the way Windows does - a
// project whose folder has a name the machine's code page cannot spell is a
// folder that is not there otherwise.
bool makeDirectory(const std::string& path) {
    if (path.empty()) return false;
    if (fs::isDirectory(path)) return true;
    return fs::ensureDirectory(path);
}

bool fileExists(const std::string& path) {
    return !path.empty() && !fs::isDirectory(path) && fs::exists(path);
}

// `key=value` lines, `#` comments, blank lines skipped. Values are trimmed, so
// a file edited by hand survives stray whitespace.
template <typename Fn>
void forEachEntry(const std::string& text, Fn fn) {
    size_t at = 0;
    while (at <= text.size()) {
        size_t end = text.find('\n', at);
        const bool last = end == std::string::npos;
        if (last) end = text.size();

        const std::string line = str::trim(text.substr(at, end - at));
        at = end + 1;
        if (!line.empty() && line[0] != '#') {
            const size_t equals = line.find('=');
            if (equals != std::string::npos)
                fn(str::trim(line.substr(0, equals)), str::trim(line.substr(equals + 1)));
        }
        if (last) break;
    }
}

bool parseFloat(const std::string& text, float& out) {
    if (text.empty()) return false;
    char* stop = nullptr;
    const double value = std::strtod(text.c_str(), &stop);
    if (stop == text.c_str() || !std::isfinite(value)) return false;
    out = (float)value;
    return true;
}

bool parseInt(const std::string& text, int& out) {
    if (text.empty()) return false;
    char* stop = nullptr;
    const long value = std::strtol(text.c_str(), &stop, 10);
    if (stop == text.c_str()) return false;
    if (value < -2147483647L || value > 2147483647L) return false;
    out = (int)value;
    return true;
}

// UTC, `YYYY-MM-DDTHH:MM:SSZ`. Empty when the platform clock misbehaves, and the
// manifest then simply has a blank timestamp.
std::string utcTimestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm parts{};
#if defined(_WIN32)
    if (gmtime_s(&parts, &now) != 0) return {};
#else
    if (gmtime_r(&now, &parts) == nullptr) return {};
#endif
    char buffer[32];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &parts) == 0) return {};
    return buffer;
}

std::string directoryOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

// A project directory can be addressed by `path` or by `manifestPath`; both
// helpers work from whichever is present so a ProjectInfo built by hand still
// resolves.
std::string projectDirectoryOf(const ProjectInfo& info) {
    if (!info.path.empty()) return info.path;
    if (!info.manifestPath.empty()) return directoryOf(info.manifestPath);
    return {};
}

// Whether two names are one file on this platform. On Windows the filesystem
// does not tell "project.spectra" and "project.Spectra" apart, and everywhere
// else it does.
bool sameFileName(const std::string& a, const std::string& b) {
#if defined(_WIN32)
    return str::equalsIgnoreAscii(a, b);
#else
    return a == b;
#endif
}

// Whether a folder is the data folder of a built game: "MyGame_Data" beside
// the program "MyGame.exe".
bool isBuiltGameFolder(const std::string& folder) {
    const std::string suffix = kBuiltGameDataSuffix;
    if (folder.size() <= suffix.size()) return false;
    return folder.compare(folder.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Whether a path that stays inside the project: relative, no "..", no drive letter and
// no root. A manifest is a file a person edits, and a scene path in it is a
// path inside the project - never one that points somewhere else on the disk.
bool isSafeRelativePath(const std::string& path) {
    if (path.empty()) return false;
    if (path.find("..") != std::string::npos) return false;
    if (path[0] == '/' || path[0] == '\\') return false;
    if (path.size() > 1 && path[1] == ':') return false;
    for (const char c : path)
        if (c == '\n' || c == '\r') return false;
    return true;
}

// The scene of the project as a path relative to the project folder: the
// manifest says where the scene is, so the folder it is in is part of the
// answer.
std::string sceneRelativePath(const ProjectInfo& info) {
    const std::string absolutePath = ProjectLibrary::scenePath(info);
    const std::string dir = projectDirectoryOf(info);
    if (!dir.empty() && absolutePath.size() > dir.size() &&
        absolutePath.compare(0, dir.size(), dir) == 0) {
        std::string relative = absolutePath.substr(dir.size());
        while (!relative.empty() && (relative[0] == '/' || relative[0] == '\\'))
            relative.erase(relative.begin());
        if (!relative.empty()) return relative;
    }
    return kProjectSceneFile;
}

std::string formatFloat(float value) {
    char buffer[32];
    const int written = std::snprintf(buffer, sizeof(buffer), "%.4f", (double)value);
    if (written <= 0) return "0";
    return buffer;
}

bool containsTrailingSpaceOrDot(const std::string& name) {
    const char last = name[name.size() - 1];
    return last == ' ' || last == '.' || last == '\t';
}

} // namespace

LanguageTag languageFromCode(const std::string& code) {
    if (code == "uk") return LanguageTag::Ukrainian;
    if (code == "de") return LanguageTag::German;
    if (code == "fr") return LanguageTag::French;
    if (code == "it") return LanguageTag::Italian;
    if (code == "es") return LanguageTag::Spanish;
    if (code == "lol") return LanguageTag::Lolcat;
    if (code == "jeb") return LanguageTag::Jeb;
    if (code == "pir") return LanguageTag::Pirate;
    if (code == "u8f") return LanguageTag::BrokenUtf8;
    if (code == "hex") return LanguageTag::Hex;
    if (code == "gd") return LanguageTag::GeometryDash;
    if (code == "pt") return LanguageTag::Portuguese;
    if (code == "mc") return LanguageTag::Minecraft;
    if (code == "ut") return LanguageTag::Undertale;
    if (code == "cel") return LanguageTag::Celeste;
    if (code == "dm") return LanguageTag::Doom;
    if (code == "wows") return LanguageTag::WolfOfWallStreet;
    if (code == "sus") return LanguageTag::AmongUs;
    if (code == "leet") return LanguageTag::Leet;
    if (code == "tlh") return LanguageTag::Klingon;
    if (code == "gop") return LanguageTag::Gopnik;
    if (code == "sha") return LanguageTag::Shakespeare;
    return LanguageTag::English;
}

ThemeTag themeTagFromCode(const std::string& code) {
    if (code == "light") return ThemeTag::Light;
    if (code == "contrast") return ThemeTag::Contrast;
    return ThemeTag::Dark;
}

// --- ProjectLibrary ---------------------------------------------------------

void ProjectLibrary::setRoot(const std::string& root) { root_ = str::trim(root); }

bool ProjectLibrary::isValidName(const std::string& name) {
    if (name.size() < 3 || name.size() > 48) return false;
    if (name == "." || name == "..") return false;
    if (name.find("..") != std::string::npos) return false;

    for (const char c : name) {
        const unsigned char byte = (unsigned char)c;
        if (byte < 0x20) return false;                          // control characters
        if (c == '/' || c == '\\') return false;                // path separators
        if (c == ':' || c == '*' || c == '?' || c == '"' ||     // Windows rejects these
            c == '<' || c == '>' || c == '|')
            return false;
    }
    // Windows silently strips a trailing dot or space, which would make the
    // directory name differ from what the manifest says.
    return !containsTrailingSpaceOrDot(name);
}

std::string ProjectLibrary::scenePath(const ProjectInfo& p) {
    const std::string dir = projectDirectoryOf(p);
    const std::string scenes = str::join(dir, kProjectScenesDir);

    // A project that says which file its scene is in, uses that file. One that
    // says nothing is a project that has never been saved, and it gets the
    // scene file the engine writes.
    if (p.sceneFile.empty()) return str::join(scenes, kProjectSceneFile);

    // A path with a separator in it is used as it is - it says where the scene
    // is inside the project.
    if (p.sceneFile.find('/') != std::string::npos ||
        p.sceneFile.find('\\') != std::string::npos)
        return str::join(dir, p.sceneFile);

    // A bare name is looked for in `scenes/` first, because that is where a
    // scene lives now, and next to the project after that - which is where the
    // projects that predate the folder keep theirs.
    const std::string inScenes = str::join(scenes, p.sceneFile);
    if (fileExists(inScenes)) return inScenes;
    const std::string beside = str::join(dir, p.sceneFile);
    if (fileExists(beside)) return beside;
    return inScenes;
}

std::string ProjectLibrary::scriptsPath(const ProjectInfo& p) {
    return str::join(projectDirectoryOf(p), kProjectScriptsDir);
}

std::string ProjectLibrary::assetsPath(const ProjectInfo& p) {
    return str::join(projectDirectoryOf(p), kProjectAssetsDir);
}

std::string ProjectLibrary::libPath(const ProjectInfo& p) {
    return str::join(projectDirectoryOf(p), kProjectLibDir);
}

std::string ProjectLibrary::modelsPath(const ProjectInfo& p) {
    return str::join(assetsPath(p), assetdirs::kModels[0]);
}

std::string ProjectLibrary::iconPath(const ProjectInfo& p) {
    const std::string dir = projectDirectoryOf(p);
    const std::string ico = str::join(dir, "icon.ico");
    if (fileExists(ico)) return ico;
    const std::string svg = str::join(dir, "icon.svg");
    if (fileExists(svg)) return svg;
    return ico;
}

std::string ProjectLibrary::relativeToProject(const ProjectInfo& p, const std::string& path) {
    const std::string dir = projectDirectoryOf(p);
    if (dir.empty() || path.empty()) return path;
    if (path.size() <= dir.size()) return path;
    if (path.compare(0, dir.size(), dir) != 0) return path;

    std::string relative = path.substr(dir.size());
    while (!relative.empty() && (relative[0] == '/' || relative[0] == '\\'))
        relative.erase(relative.begin());
    return relative.empty() ? path : relative;
}

void ProjectLibrary::scan() {
    projects_.clear();

    if (root_.empty()) return;

    std::vector<fs::DirEntry> entries;
    // A missing or unreadable root is the normal first-run state, so it yields
    // an empty list rather than an error.
    if (!fs::listDirectoryEx(root_, entries)) return;

    for (const fs::DirEntry& entry : entries) {
        if (!entry.isDir) continue;
        const std::string path = str::join(root_, entry.name);
        if (!fileExists(manifestPath(path))) continue;

        ProjectInfo info;
        if (!readManifest(path, info)) continue;
        if (info.valid) projects_.push_back(info);
    }

    std::sort(projects_.begin(), projects_.end(),
              [](const ProjectInfo& a, const ProjectInfo& b) {
                  return str::toLowerAscii(a.name) < str::toLowerAscii(b.name);
              });

    Log::info("Projects: %d in '%s'", (int)projects_.size(), root_.c_str());
}

ProjectInfo* ProjectLibrary::projectAt(int index) {
    if (index < 0 || index >= (int)projects_.size()) return nullptr;
    return &projects_[(size_t)index];
}

const ProjectInfo* ProjectLibrary::projectAt(int index) const {
    if (index < 0 || index >= (int)projects_.size()) return nullptr;
    return &projects_[(size_t)index];
}

ProjectInfo ProjectLibrary::create(const std::string& name, LanguageTag language) {
    const std::string clean = str::trim(name);
    if (!isValidName(clean)) {
        Log::error("Project: invalid name '%s' (need 3-48 chars, no separators or '..')",
                    name.c_str());
        return {};
    }
    if (root_.empty()) {
        Log::error("Project: no project root set");
        return {};
    }

    const std::string path = str::join(root_, clean);

    // Case-insensitive: on Windows "Demo" and "demo" are the same directory,
    // and creating one twice would silently reuse the first.
    std::vector<fs::DirEntry> existing;
    if (fs::listDirectoryEx(root_, existing)) {
        for (const fs::DirEntry& entry : existing)
            if (entry.isDir && str::equalsIgnoreAscii(entry.name, clean)) {
                Log::error("Project: '%s' already exists in '%s'", clean.c_str(), root_.c_str());
                return {};
            }
    }

    // The tree of a project:
    //
    //   assets/{textures,audio,music,shaders,material,fonts,models,sky,lang,
    //           icons,prefabs,ui,data,particles,physics}
    //   lib/            the core of the engine
    //   scripts/        the scripts of the game
    //   scenes/         the scenes of the game
    //   project.Spectra
    //   icon.ico
    //
    // The folders are made empty rather than not made at all: a project that
    // has no `assets/textures` folder is a project whose textures go
    // somewhere else, and where that is depends on who is looking.
    const std::string assets = str::join(path, kProjectAssetsDir);
    if (!makeDirectory(path) || !makeDirectory(assets) ||
        !makeDirectory(str::join(path, kProjectLibDir)) ||
        !makeDirectory(str::join(path, kProjectScriptsDir)) ||
        !makeDirectory(str::join(path, kProjectScenesDir))) {
        Log::error("Project: cannot create directory tree under '%s'", path.c_str());
        return {};
    }
    for (const char* folder : {"textures", "audio", "music", "shaders", "material",
                               "fonts", "models", "sky", "lang", "icons", "prefabs",
                               "ui", "data", "particles", "physics"}) {
        if (!makeDirectory(str::join(assets, folder))) {
            Log::error("Project: cannot create '%s' under '%s'", folder, path.c_str());
            return {};
        }
    }

    ProjectInfo info;
    info.name = str::fileName(path);
    info.path = path;
    info.manifestPath = str::join(path, kProjectManifestFile);
    info.version = kManifestVersion;
    info.created = utcTimestamp();
    info.modified = info.created;
    info.language = languageCode(language);
    info.gravity = 2;  // GravityAxis::NegY
    info.cameraPosition = Vec3{kDefaultCameraX, kDefaultCameraY, kDefaultCameraZ};
    info.cameraYaw = 0.0f;
    info.cameraPitch = 0.0f;
    info.cameraFov = 70.0f;
    info.valid = true;

    // The editor refuses to load an empty scene, but the file has to exist from
    // the start so the project directory is self-describing before the first save.
    if (!fs::writeFile(scenePath(info), "")) {
        Log::error("Project: cannot create '%s'", scenePath(info).c_str());
        info.valid = false;
        return info;
    }

    // The picture of the project: the engine's own icon, so the launcher has
    // something to show and the project is not the only one without a face.
    const std::string engineIcon = str::join(str::join(fs::assetRoot(), "image/icons"),
                                              kAppIconFile);
    if (fileExists(engineIcon))
        fs::copyTree(engineIcon, str::join(path, kProjectIconFile));

    // The core of the engine, so the project carries what it runs on. A project
    // without one is not broken: it runs on the engine it is opened with.
    const std::string core = findEngineCore();
    if (!core.empty()) fs::copyTree(core, str::join(str::join(path, kProjectLibDir),
                                                   str::fileName(core)));

    if (!writeManifest(info)) {
        info.valid = false;
        return info;
    }

    Log::info("Project: created '%s' at '%s'", info.name.c_str(), info.path.c_str());
    return info;
}

bool ProjectLibrary::readManifest(const std::string& projectPath, ProjectInfo& out) const {
    ProjectInfo info;
    // The directory name is the identity: the `name=` key exists for tools
    // reading the manifest without touching the filesystem.
    info.name = str::fileName(projectPath);
    info.path = projectPath;
    info.manifestPath = manifestPath(projectPath);

    std::string text;
    if (!fs::readFile(info.manifestPath, text)) {
        out = info;
        out.valid = false;
        return false;
    }

    forEachEntry(text, [&info](const std::string& key, const std::string& value) {
        float number = 0.0f;
        int integer = 0;
        if (key == "name") {
            // The directory name is the identity: the `name=` key exists for
            // tools reading the manifest without touching the filesystem.
            //
            // A built game is the exception: its data is in a folder named
            // after the program - "MyGame_Data" - and the name in the manifest
            // is the name of the game. That name is the one that is used, and
            // it is not a mistake.
            const std::string wanted = str::trim(value);
            if (wanted.empty() || wanted == info.name) {
                // The same name: nothing to say.
            } else if (isBuiltGameFolder(info.name)) {
                info.name = wanted;
            } else {
                Log::warn("Project: manifest name '%s' does not match its folder",
                          value.c_str());
            }
        } else if (key == "version") {
            if (parseInt(value, integer) && integer > 0) {
                info.version = integer;
                if (integer > kManifestVersion)
                    Log::warn("Project: '%s' was written by a newer engine (manifest v%d)",
                              info.name.c_str(), integer);
            }
        } else if (key == "created") {
            info.created = value;
        } else if (key == "modified") {
            info.modified = value;
        } else if (key == "language") {
            info.language = languageCode(languageFromCode(value));
        } else if (key == "gravity") {
            if (parseInt(value, integer)) info.gravity = std::clamp(integer, kGravityMin, kGravityMax);
        } else if (key == "cameraX" && parseFloat(value, number)) {
            info.cameraPosition.x = number;
        } else if (key == "cameraY" && parseFloat(value, number)) {
            info.cameraPosition.y = number;
        } else if (key == "cameraZ" && parseFloat(value, number)) {
            info.cameraPosition.z = number;
        } else if (key == "cameraYaw" && parseFloat(value, number)) {
            info.cameraYaw = number;
        } else if (key == "cameraPitch" && parseFloat(value, number)) {
            info.cameraPitch = clampf(number, -kPitchLimit, kPitchLimit);
} else if (key == "cameraFov" && parseFloat(value, number)) {
            info.cameraFov = clampf(number, kFovMin, kFovMax);
        } else if (key == "favorite") {
            if (parseInt(value, integer)) info.favorite = integer != 0;
        } else if (key == "shadowQuality") {
            if (parseInt(value, integer)) info.shadowQuality = std::clamp(integer, 0, 3);
        } else if (key == "shadowResolution") {
            if (parseInt(value, integer)) info.shadowResolution = std::clamp(integer, 256, 8192);
        } else if (key == "shadowCascades") {
            if (parseInt(value, integer)) info.shadowCascades = std::clamp(integer, 1, 4);
        } else if (key == "shadowSunAzimuth") {
            if (parseFloat(value, number)) info.shadowSunAzimuth = clampf(number, 0.0f, 360.0f);
        } else if (key == "shadowSunElevation") {
            if (parseFloat(value, number)) info.shadowSunElevation = clampf(number, 1.0f, 89.0f);
        } else if (key == "shadowBias") {
            if (parseFloat(value, number)) info.shadowBias = clampf(number, 0.0f, 0.5f);
        } else if (key == "shadowNormalOffset") {
            if (parseFloat(value, number)) info.shadowNormalOffset = clampf(number, 0.0f, 8.0f);
        } else if (key == "shadowRadius") {
            if (parseFloat(value, number)) info.shadowRadius = clampf(number, 0.5f, 32.0f);
        } else if (key == "scene") {
            // The scene file inside the project, as a path relative to the
            // project folder. A project that keeps its scene in JSON says so
            // here, and a project that says nothing keeps the text format.
            const std::string name = str::trim(value);
            if (name.empty()) {
                // Nothing to say.
            } else if (isSafeRelativePath(name)) {
                info.sceneFile = name;
            } else {
                Log::warn("Project: ignoring a scene path that leaves the project ('%s')",
                          name.c_str());
            }
        }
        // Unknown keys are ignored: a newer build may add some.
    });

    info.valid = true;
    out = info;
    return true;
}

bool ProjectLibrary::writeManifest(const ProjectInfo& info) const {
    if (!info.valid || info.path.empty()) {
        Log::error("Project: refusing to write a manifest for '%s'",
                    info.path.empty() ? "<no path>" : info.path.c_str());
        return false;
    }

    // Fixed key order, always all thirteen keys: the file is meant to be
    // diffable and hand-editable, so it must not depend on what the caller
    // happened to fill in.
    std::string text;
    text.reserve(320);
    text += "# Spectra3D project manifest\n";
    text += "name=" + info.name + "\n";
    text += "version=" + std::to_string(info.version > 0 ? info.version : kManifestVersion) + "\n";
    text += "engine=";
    text += kEngineName;
    text += ' ';
    text += kEngineVersion;
    text += "\n";
    text += "created=" + info.created + "\n";
    text += "modified=" + info.modified + "\n";
    text += "language=" + info.language + "\n";
    text += "gravity=" +
            std::to_string(std::clamp(info.gravity, kGravityMin, kGravityMax)) + "\n";
    text += "cameraX=" + formatFloat(info.cameraPosition.x) + "\n";
    text += "cameraY=" + formatFloat(info.cameraPosition.y) + "\n";
    text += "cameraZ=" + formatFloat(info.cameraPosition.z) + "\n";
    text += "cameraYaw=" + formatFloat(info.cameraYaw) + "\n";
    text += "cameraPitch=" + formatFloat(info.cameraPitch) + "\n";
text += "cameraFov=" + formatFloat(info.cameraFov) + "\n";
    text += "favorite=";
    text += info.favorite ? "1" : "0";
    text += "\n";
    // The shadows: how they are drawn, how big the map is, and how many
    // stretches of the scene get a map of their own.
    text += "shadowQuality=" + std::to_string(std::clamp(info.shadowQuality, 0, 3)) + "\n";
    text += "shadowResolution=" +
            std::to_string(std::clamp(info.shadowResolution, 256, 8192)) + "\n";
    text += "shadowCascades=" + std::to_string(std::clamp(info.shadowCascades, 1, 4)) + "\n";
    // The sun and the two numbers that decide how a shadow is compared with
    // the map. The sun is where the light comes from; the bias and the
    // normal offset are how far a surface is pushed before it is compared.
    text += "shadowSunAzimuth=" + formatFloat(info.shadowSunAzimuth) + "\n";
    text += "shadowSunElevation=" + formatFloat(info.shadowSunElevation) + "\n";
    text += "shadowBias=" + formatFloat(info.shadowBias) + "\n";
    text += "shadowNormalOffset=" + formatFloat(info.shadowNormalOffset) + "\n";
    text += "shadowRadius=" + formatFloat(info.shadowRadius) + "\n";
    // The scene file the project keeps its scene in, as a path relative to the
    // project folder. The resolved name, so a project that has not migrated yet
    // keeps saying what it is using.
    text += "scene=" + sceneRelativePath(info) + "\n";

    const std::string path = str::join(info.path, kProjectManifestFile);
    if (!fs::writeFile(path, text)) {
        Log::error("Project: cannot write '%s'", path.c_str());
        return false;
    }

    // The manifest under its old name is the same file with an older spelling.
    // On Windows writing the new one has already replaced it - the filesystem
    // does not tell the two names apart - and elsewhere it is still there, and
    // two manifests in one folder is one too many.
    const std::string legacy = str::join(info.path, kProjectManifestLegacy);
    if (!sameFileName(kProjectManifestLegacy, kProjectManifestFile) && fileExists(legacy)) {
        fs::removeTree(legacy);
        Log::info("Project: the manifest is now '%s'", kProjectManifestFile);
    }
    return true;
}

std::string ProjectLibrary::manifestPath(const std::string& projectPath) {
    const std::string now = str::join(projectPath, kProjectManifestFile);
    if (fileExists(now)) return now;
    const std::string legacy = str::join(projectPath, kProjectManifestLegacy);
    if (fileExists(legacy)) return legacy;
    // A built game calls its manifest the boot file.
    const std::string boot = str::join(projectPath, kProjectBootFile);
    if (fileExists(boot)) return boot;
    return now;
}

// Moves a project that was made before the layout changed into the layout.
//
// The scene moves into `scenes/` and the models into `assets/models`, and the
// manifest is rewritten to say where the scene is. A project that is already in
// the layout is left exactly as it is, and nothing is ever moved over a file
// that is already there: a project with two scenes keeps both.
bool ProjectLibrary::migrate(ProjectInfo& info) const {
    const std::string dir = projectDirectoryOf(info);
    if (dir.empty()) return false;

    // A built game is not a project to be moved: its data is already in the
    // layout, and the manifest in it is the boot file. Moving it would leave a
    // second manifest beside the boot file, and the game would read that one.
    if (manifestPath(dir) == str::join(dir, kProjectBootFile)) return false;

    bool changed = false;

    // The manifest under its old name is the same file with an older spelling.
    // It is written again under the new one, so the rename happens once and not
    // on every opening.
    if (manifestPath(dir) != str::join(dir, kProjectManifestFile)) changed = true;

    // The folders the layout has. Made whether or not anything is moved: an
    // empty `scenes/` is a folder that says where the scenes are.
    const std::string scenes = str::join(dir, kProjectScenesDir);
    const std::string assets = str::join(dir, kProjectAssetsDir);
    makeDirectory(scenes);
    makeDirectory(assets);

    // The scene: a file beside the project moves into the scenes folder.
    const std::string sceneFile = info.sceneFile;
    if (!sceneFile.empty() && sceneFile.find('/') == std::string::npos &&
        sceneFile.find('\\') == std::string::npos) {
        const std::string beside = str::join(dir, sceneFile);
        const std::string inScenes = str::join(scenes, sceneFile);
        if (fileExists(beside) && !fileExists(inScenes) && fs::copyTree(beside, inScenes)) {
            info.sceneFile = str::join(kProjectScenesDir, sceneFile);
            changed = true;
            Log::info("Project: the scene moved into '%s'", kProjectScenesDir);
        }
    }

    // The models: a folder beside the project moves into the assets.
    const std::string legacyModels = str::join(dir, "models");
    const std::string newModels = str::join(assets, "models");
    if (fs::isDirectory(legacyModels) && !fs::isDirectory(newModels)) {
        makeDirectory(newModels);
        std::vector<fs::DirEntry> entries;
        bool ok = true;
        if (fs::listDirectoryEx(legacyModels, entries)) {
            for (const fs::DirEntry& entry : entries)
                ok = fs::copyTree(str::join(legacyModels, entry.name),
                                  str::join(newModels, entry.name)) &&
                     ok;
        }
        if (ok) {
            fs::removeTree(legacyModels);
            Log::info("Project: the models moved into '%s/%s'", kProjectAssetsDir, "models");
        }
    }

    if (changed) writeManifest(info);
    return changed;
}

bool ProjectLibrary::setFavorite(const std::string& projectPath, bool favorite) {
    ProjectInfo info;
    if (!readManifest(projectPath, info)) {
        // Nothing to star. The caller keeps whatever it had on screen.
        Log::error("Project: cannot read the manifest of '%s'", projectPath.c_str());
        return false;
    }
    if (info.favorite == favorite) return info.favorite;

    info.favorite = favorite;
    info.modified = utcTimestamp();
    if (!writeManifest(info)) return info.favorite;  // unchanged on disk
    Log::info("Project: '%s' %s", info.name.c_str(), favorite ? "favorited" : "unfavorited");
    return favorite;
}

// --- Settings ---------------------------------------------------------------

std::string Settings::defaultProjectRoot() {
    const std::string& exeDir = fs::executableDir();
    if (exeDir.empty()) return std::string("project");

    // A built game keeps its project beside the program, in the data folder:
    // `<name>_Data`, where the manifest is `boot.spectra`.
    const std::string data = builtGameDataDir();
    if (!data.empty() && fs::isDirectory(data)) return data;

    // The folder of the projects is next to the program - and, in a built game,
    // one level above it: the program itself lives in `lib/`, next to the core
    // of the engine, and the project sits beside that.
    const std::string beside = str::join(exeDir, "project");
    if (fs::isDirectory(beside)) return beside;
    const std::string above = str::join(str::join(exeDir, ".."), "project");
    if (fs::isDirectory(above)) return above;
    return beside;
}

std::string Settings::settingsPath() {
    const std::string& exeDir = fs::executableDir();
    return exeDir.empty() ? std::string(kSettingsFileName) : str::join(exeDir, kSettingsFileName);
}

bool Settings::load() {
    const std::string path = settingsPath();
    std::string text;
    if (!fs::readFile(path, text)) return false;  // first run, not an error

    forEachEntry(text, [this](const std::string& key, const std::string& value) {
        if (key == "projectRoot") {
            projectRoot = value;
} else if (key == "language") {
            language = languageFromCode(value);
        } else if (key == "theme") {
            theme = themeTagFromCode(value);
        } else if (key == "frameRateCap") {
            int cap = 0;
            // 0 is a legitimate value (uncapped); anything else is clamped to a
            // range that still makes sense as a frame rate.
            if (parseInt(value, cap)) frameRateCap = std::clamp(cap, 0, 240);
        } else if (key == "loaderThreads") {
            int threads = 0;
            // One to six: a thread that has nothing to do is a thread that
            // costs, and a machine with two cores does not want six.
            if (parseInt(value, threads)) loaderThreads = std::clamp(threads, 1, 6);
        } else if (key == "appIcon") {
            if (!value.empty()) appIcon = value;
        } else if (key == "recent") {
            // File order is newest first, so the first occurrence wins and the
            // rest are dropped.
            if (value.empty()) return;
            for (const std::string& known : recentProjects)
                if (str::equalsIgnoreAscii(known, value)) return;
            if (recentProjects.size() < kMaxRecentProjects) recentProjects.push_back(value);
        }
    });

    if (projectRoot.empty()) projectRoot = defaultProjectRoot();
    Log::info("Settings: '%s' (root '%s', %d recent)", path.c_str(), projectRoot.c_str(),
              (int)recentProjects.size());
    return true;
}

bool Settings::save() const {
    std::string text;
    text.reserve(256);
    text += "# Spectra3D settings\n";
    text += "projectRoot=" + (projectRoot.empty() ? defaultProjectRoot() : projectRoot) + "\n";
    text += std::string("language=") + languageCode(language) + "\n";
    text += std::string("theme=") + themeTagCode(theme) + "\n";
    text += "frameRateCap=" + std::to_string(frameRateCap) + "\n";
    text += "loaderThreads=" + std::to_string(loaderThreads) + "\n";
    text += "appIcon=" + appIcon + "\n";
    for (size_t i = 0; i < recentProjects.size() && i < kMaxRecentProjects; ++i)
        text += "recent=" + recentProjects[i] + "\n";

    const std::string path = settingsPath();
    if (!fs::writeFile(path, text)) {
        Log::error("Settings: cannot write '%s'", path.c_str());
        return false;
    }
    return true;
}

void Settings::rememberProject(const std::string& path) {
    const std::string clean = str::trim(path);
    if (clean.empty()) return;

    forgetProject(clean);
    recentProjects.insert(recentProjects.begin(), clean);
    if (recentProjects.size() > kMaxRecentProjects) recentProjects.resize(kMaxRecentProjects);
}

void Settings::forgetProject(const std::string& path) {
    const std::string clean = str::trim(path);
    if (clean.empty()) return;
    for (size_t i = 0; i < recentProjects.size();) {
        if (str::equalsIgnoreAscii(recentProjects[i], clean))
            recentProjects.erase(recentProjects.begin() + (std::ptrdiff_t)i);
        else
            ++i;
    }
}

// --- shared -----------------------------------------------------------------

bool projectExists(const std::string& path) {
    if (path.empty() || !fs::isDirectory(path)) return false;
    return fileExists(ProjectLibrary::manifestPath(path));
}

// The core of the engine as a file.
//
// It is looked for next to the program first - that is where the engine keeps
// the core it runs on - and in the `lib/` folder of the program after that,
// which is the layout of a built game.
std::string findEngineCore() {
    const std::string& exeDir = fs::executableDir();
    if (exeDir.empty()) return {};

    for (const char* name : kEngineCoreNames) {
        const std::string path = str::join(exeDir, name);
        if (fileExists(path)) return path;
    }
    for (const char* name : kEngineCoreNames) {
        const std::string path = str::join(str::join(exeDir, kProjectLibDir), name);
        if (fileExists(path)) return path;
    }
    return {};
}

// The data folder of a built game: "<exeDir>/<name>_Data".
std::string builtGameDataDir() {
    const std::string& exe = fs::executablePath();
    if (exe.empty()) return {};
    std::string name = str::fileName(exe);
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    if (name.empty()) return {};
    return str::join(fs::executableDir(), name + kBuiltGameDataSuffix);
}

} // namespace s3d