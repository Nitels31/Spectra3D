#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "core/Math.hpp"

namespace s3d {

// Engine identity. Stamped into every project manifest (`engine=`), so the
// launcher and the manifest agree on one version string.
inline constexpr const char* kEngineName = "Spectra3D";
inline constexpr const char* kEngineVersion = "1.0.0";

// On-disk names inside a project directory. Prefixed because the editor and the
// script layer own their own `scene.json` / `scripts` literals at namespace
// scope, and two internal-linkage constants of the same name in one translation
// unit is a trap worth not walking into.
inline constexpr const char* kProjectManifestFile = "project.spectra";
inline constexpr const char* kProjectSceneFile = "scene.json";
inline constexpr const char* kProjectScriptsDir = "scripts";
inline constexpr const char* kProjectModelsDir = "models";
// Where the editor keeps the workspace it was left in: dock sizes, which panels
// are open and where each one floats. Per project, like the scene - two projects
// are two workspaces.
inline constexpr const char* kEditorLayoutFile = "editorlayout.txt";
inline constexpr const char* kSettingsFileName = "settings.ini";
// The application icon, shipped with the rest of the images and pickable in the
// launcher's settings. One file for the window, the task bar and - when a resource
// compiler exists - the executable itself. A build that does not have the file
// still gets an icon: Window::applyIcon() draws one.
//
// The directory is a constant rather than a lookup because it is the one place the
// runtime, the build script and the settings screen have to agree on; the icon
// files themselves are listed at runtime, so dropping another .ico or .png in
// there is all it takes to make it appear in the picker.
inline constexpr const char* kAppIconFile = "icon.ico";
inline constexpr const char* kAppIconsDir = "assets/image/icons";

// Languages a project can be tagged with. Deliberately not i18n::Language:
// core must not depend on the localisation layer, so the launcher is the one
// place that translates between the two.
enum class LanguageTag {
    English, Ukrainian, German, French, Italian, Spanish, Lolcat,
    Jeb, Pirate, BrokenUtf8, Hex, GeometryDash,
    Portuguese, Minecraft, Undertale, Celeste, Doom, WolfOfWallStreet,
    AmongUs, Leet, Klingon, Gopnik, Shakespeare,
};

inline const char* languageCode(LanguageTag tag) {
    switch (tag) {
        case LanguageTag::Ukrainian: return "uk";
        case LanguageTag::German: return "de";
        case LanguageTag::French: return "fr";
        case LanguageTag::Italian: return "it";
        case LanguageTag::Spanish: return "es";
        case LanguageTag::Lolcat: return "lol";
        case LanguageTag::Jeb: return "jeb";
        case LanguageTag::Pirate: return "pir";
        case LanguageTag::BrokenUtf8: return "u8f";
        case LanguageTag::Hex: return "hex";
        case LanguageTag::GeometryDash: return "gd";
        case LanguageTag::Portuguese: return "pt";
        case LanguageTag::Minecraft: return "mc";
        case LanguageTag::Undertale: return "ut";
        case LanguageTag::Celeste: return "cel";
        case LanguageTag::Doom: return "dm";
        case LanguageTag::WolfOfWallStreet: return "wows";
        case LanguageTag::AmongUs: return "sus";
        case LanguageTag::Leet: return "leet";
        case LanguageTag::Klingon: return "tlh";
        case LanguageTag::Gopnik: return "gop";
        case LanguageTag::Shakespeare: return "sha";
        case LanguageTag::English:
        default: return "en";
    }
}

// Unknown or empty codes fall back to English rather than failing: a manifest
// written by a newer build must still open.
LanguageTag languageFromCode(const std::string& code);

// UI palettes. Lives in core next to LanguageTag for the same reason: core owns
// settings.ini and must not depend on the ui layer, so the translation between
// this and theme::ThemeId happens in the launcher, the one place that knows both.
enum class ThemeTag { Dark, Light, Contrast };

inline const char* themeTagCode(ThemeTag tag) {
    switch (tag) {
        case ThemeTag::Light: return "light";
        case ThemeTag::Contrast: return "contrast";
        case ThemeTag::Dark:
        default: return "dark";
    }
}

// Unknown codes fall back to Dark instead of failing.
ThemeTag themeTagFromCode(const std::string& code);

// A project as it exists on disk.
struct ProjectInfo {
    std::string name;        // directory name
    std::string path;        // absolute path to the project directory
    std::string manifestPath;
    std::string created, modified;
    int version = 1;
    std::string language = "en";
    int gravity = 2;         // GravityAxis value; 2 == NegY
    Vec3 cameraPosition;
    float cameraYaw = 0, cameraPitch = 0, cameraFov = 70;
    // Starred in the launcher's Favorite tab. Lives in the manifest, so it
    // belongs to the project rather than to this machine's settings.ini.
    bool favorite = false;
    // The scene file the project keeps its scene in, inside the project folder.
    // Empty means the project does not say, which is every project made before
    // the scene became JSON: those keep the text file they have until the first
    // time they are saved.
    std::string sceneFile;
    bool valid = false;
};

// Global, machine-level settings. Persisted next to the binary (portable), in
// the same `key=value` text format as the manifest.
struct Settings {
    std::string projectRoot;                     // folder that CONTAINS projects
    std::vector<std::string> recentProjects;     // absolute paths, newest first
    LanguageTag language = LanguageTag::English;
    ThemeTag theme = ThemeTag::Dark;

    // Upper bound on rendered frames per second; 0 means uncapped. Vsync is not
    // a cap on its own - several drivers report well over 100 FPS for a static
    // image - so this is the only thing standing between a laptop and a fan.
    int frameRateCap = 60;

// Which icon file to use, by name only - the path is always
    // assets/image/icons/, never something settings.ini can redirect. Switching it
    // repaints the window and the task bar entry immediately and survives the next
    // launch. A name that is not there falls back to the drawn mark, so a stale or
    // hand-edited value costs the icon and nothing else.
    std::string appIcon = kAppIconFile;

    // False when the file is missing or unreadable, which is not an error: the
    // defaults below are then the state. Keys that are absent keep the default.
    bool load();
    bool save() const;

    // Moves `path` to the front of the recents, de-duplicating case
    // insensitively (Windows paths are case insensitive, Linux ones are not).
    void rememberProject(const std::string& path);
    void forgetProject(const std::string& path);

    static std::string defaultProjectRoot();     // <exeDir>/project

    // Directory the settings file lives in: next to the binary when known.
    static std::string settingsPath();
};

// Number of entries kept in Settings::recentProjects.
inline constexpr size_t kMaxRecentProjects = 8;

class ProjectLibrary {
public:
    void setRoot(const std::string& root);
    const std::string& root() const { return root_; }

    // Fills projects() with every subdirectory of the root that carries a
    // manifest, sorted case-insensitively by name.
    void scan();
    const std::vector<ProjectInfo>& projects() const { return projects_; }

    // Creates the directory tree plus the manifest. Returns the new project, or
    // an invalid ProjectInfo on failure (with the reason in the log).
    ProjectInfo create(const std::string& name, LanguageTag language);

    bool readManifest(const std::string& projectPath, ProjectInfo& out) const;
    // Writes `info` verbatim - timestamps are the caller's business.
    bool writeManifest(const ProjectInfo& info) const;
    // Reads the manifest, flips `favorite` and writes it back. Returns the new
    // state, or the state on disk when the write failed.
    bool setFavorite(const std::string& projectPath, bool favorite);

    static std::string scenePath(const ProjectInfo& p);
    static std::string scriptsPath(const ProjectInfo& p);
    static std::string modelsPath(const ProjectInfo& p);

    // 3-48 characters, no path separators, no "..", and nothing a filesystem
    // would silently rewrite (`: * ? " < > |`, control characters, a trailing
    // dot or space).
    static bool isValidName(const std::string& name);

private:
    std::string root_;
    std::vector<ProjectInfo> projects_;
};

// True when `path` is a directory that carries a manifest.
bool projectExists(const std::string& path);

} // namespace s3d