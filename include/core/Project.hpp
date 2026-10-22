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
//
// The project is a folder that holds everything a game needs, and it is laid out
// the way a project is expected to be:
//
//   <Project>/
//     assets/        the content: textures, audio, shaders, materials, fonts,
//                    models and the rest
//     lib/           the core of the engine, as the library it is
//     scripts/       the scripts of the game
//     scenes/        the scenes of the game
//     project.Spectra   the manifest: what the project is
//     icon.ico       the picture of the project
inline constexpr const char* kProjectManifestFile = "project.Spectra";
// The name the manifest had before it was given its extension in capitals. It is
// still read, so a project made before the rename keeps opening - and it is the
// same file on Windows, where the two spellings are one file.
inline constexpr const char* kProjectManifestLegacy = "project.spectra";
// The manifest of a built game. A built game is one program and one folder of
// data, and the folder is `<name>_Data` beside the program: the manifest in it
// is the boot file, and it says where the scenes and the scripts are.
inline constexpr const char* kProjectBootFile = "boot.spectra";
// The folder of a built game that holds the data of the game, beside the
// program that reads it.
inline constexpr const char* kBuiltGameDataSuffix = "_Data";
// The folder of a built game that holds what the engine brings: its
// languages, its font, its sounds and its sky.
inline constexpr const char* kBuiltGamePluginsDir = "plugins";
inline constexpr const char* kProjectAssetsDir = "assets";
inline constexpr const char* kProjectLibDir = "lib";
inline constexpr const char* kProjectScriptsDir = "scripts";
inline constexpr const char* kProjectScenesDir = "scenes";
inline constexpr const char* kProjectSceneFile = "scene.json";
inline constexpr const char* kProjectIconFile = "icon.ico";

// The core of the engine, as the library it is. The name depends on the
// platform, and it is looked for next to the program and in the `lib/` folder
// of a project - a project carries the core it was made with.
inline constexpr const char* kEngineCoreNames[] = {
    "SpectraPlayer.dll",
    "libSpectraPlayer.so",
    "libSpectraPlayer.dylib",
};
// The player: the program that plays a game. The engine, a window and a loop,
// with no editor and no launcher in it.
//
// It is built next to the editor and copied into a build under the name of the
// game, so it is looked for next to the running program.
inline constexpr const char* kPlayerProgramNames[] = {
    "SpectraPlayer.exe",
    "SpectraPlayer",
    "spectra_player.exe",
    "spectra_player",
};
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

// The folders a kind of asset lives in.
//
// A project keeps its content in `assets/` under plain names: `textures/`,
// `audio/`, `shaders/`, `material/`, `fonts/`, `models/` and the rest. The
// engine ships its own defaults, and those live in the older spelling
// (`image/textures`, `sounds`, `image/icons`), so both are looked for and the
// project's wins - a project that has a texture of its own shows it, and the
// engine's is still there for everything the project did not bring.
namespace assetdirs {

inline constexpr const char* kTextures[] = {"textures", "image/textures"};
inline constexpr const char* kAudio[] = {"audio", "sounds"};
inline constexpr const char* kMusic[] = {"music"};
inline constexpr const char* kShaders[] = {"shaders"};
inline constexpr const char* kMaterial[] = {"material"};
inline constexpr const char* kFonts[] = {"fonts"};
inline constexpr const char* kModels[] = {"models"};
inline constexpr const char* kSky[] = {"sky"};
inline constexpr const char* kLang[] = {"lang"};
inline constexpr const char* kIcons[] = {"icons", "image/icons"};
inline constexpr const char* kPrefabs[] = {"prefabs"};
inline constexpr const char* kUi[] = {"ui"};
inline constexpr const char* kData[] = {"data"};
inline constexpr const char* kParticles[] = {"particles"};
inline constexpr const char* kPhysics[] = {"physics"};

} // namespace assetdirs

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
    // A path relative to the project folder ("scenes/scene.json"), because a
    // project keeps its scenes in `scenes/` and the manifest has to say so.
    //
    // Empty means the project does not say, which is every project made before
    // the scene became JSON: those keep the text file they have until the first
    // time they are saved.
    std::string sceneFile;
    // The shadows: how they are drawn, how big the map is, and how many
    // stretches of the scene get a map of their own.
    //
    // They are the project's and not the machine's: a scene that looks the
    // same on another machine is the same scene, and the shadows are a part
    // of how it looks.
    //
    // The quality is the number the shadow's modes are numbered by: 0 off,
    // 1 one tap, 2 the filtered one, 3 the soft one.
    //
    // The default is the filtered one: a stair-stepped edge is the first
    // thing the eye picks out of a picture, and the sixteen reads it costs
    // are cheap.
    int shadowQuality = 2;
    int shadowResolution = 2048;
    int shadowCascades = 4;
    // The sun, and the two numbers that decide how a shadow is compared with
    // the map. The sun is the direction the light comes from; the bias is how
    // far the surface is pushed towards the light before it is compared, in
    // metres; the normal offset is the same push along the surface's own
    // normal, in texels of the map.
    float shadowSunAzimuth = 41.2f;
    float shadowSunElevation = 58.6f;
    float shadowBias = 0.015f;
    float shadowNormalOffset = 2.0f;
    // The widest the penumbra of the soft mode may get, in texels.
    float shadowRadius = 12.0f;
    // The fog: the air between the eye and the thing.
    //
    // It is the project's for the same reason the shadows are: a scene that
    // looks the same on another machine is the same scene, and the weather of
    // a scene is a part of how it looks.
    //
    // The numbers are the ones the fog is made of: how thick it is where it
    // is thickest (1/m), how fast it thins as it goes up (1/m), the height at
    // which it is thickest (m), how much of the sun's light it carries, and
    // how forward it throws that light. Zero density is no fog at all.
    float fogDensity = 0.0f;
    float fogFalloff = 0.04f;
    float fogHeight = 0.0f;
    float fogScatter = 0.25f;
    float fogForward = 0.7f;
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

    // How many threads the loader may use. One is the same as no threads at
    // all; six is what a machine with a lot of cores can actually use for
    // reading a folder of models.
    int loaderThreads = 4;

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
    // The project at that place in the list, or nullptr when the index is out
    // of range. The list is what the launcher shows, and the settings of one
    // of them are edited through it.
    ProjectInfo* projectAt(int index);
    const ProjectInfo* projectAt(int index) const;

    // Creates the directory tree plus the manifest. Returns the new project, or
    // an invalid ProjectInfo on failure (with the reason in the log).
    ProjectInfo create(const std::string& name, LanguageTag language);

    bool readManifest(const std::string& projectPath, ProjectInfo& out) const;
    // Writes `info` verbatim - timestamps are the caller's business.
    bool writeManifest(const ProjectInfo& info) const;
    // The manifest of the project in that folder: the file it is written under,
    // and the one it had before the rename if that is what is there.
    static std::string manifestPath(const std::string& projectPath);
    // Moves a project that was made before the layout changed into the layout:
    // the scene into `scenes/`, the models into `assets/models`. A project that
    // is already in the layout is left exactly as it is.
    bool migrate(ProjectInfo& info) const;
    // Reads the manifest, flips `favorite` and writes it back. Returns the new
    // state, or the state on disk when the write failed.
    bool setFavorite(const std::string& projectPath, bool favorite);

    // Where the pieces of the project are. All of them are inside the project
    // folder, and none of them is guessed: a path that is asked for is a path
    // the project has.
    //
    // The scene is the one that is not a plain join: the manifest says which
    // file it is in, because a project may have more than one scene.
    static std::string scenePath(const ProjectInfo& p);
    static std::string scriptsPath(const ProjectInfo& p);
    static std::string assetsPath(const ProjectInfo& p);
    static std::string libPath(const ProjectInfo& p);
    static std::string modelsPath(const ProjectInfo& p);
    // The picture of the project: icon.ico, or icon.svg when there is one.
    static std::string iconPath(const ProjectInfo& p);
    // The path of a file inside the project, relative to the project folder.
    // The manifest holds paths this way, so a project folder can be moved and
    // the project still opens.
    static std::string relativeToProject(const ProjectInfo& p, const std::string& path);

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

// The core of the engine as a file: next to the program, or in the `lib/`
// folder of a project. Empty when there is none - a project that carries no
// core is a project that runs on the engine it is opened with.
std::string findEngineCore();

// The player: the program that plays a game, next to the running program.
// Empty when there is none - a build of a game is made of it, and without it
// a build is a copy of the editor.
std::string findPlayerProgram();

// The data folder of a built game: "<exeDir>/<name>_Data", where <name> is the
// name of the program without the extension.
//
// A built game is one program and one folder of data: `boot.spectra` is the
// manifest in it, `scenes/` holds the scenes, `scripts/` the scripts, `assets/`
// the game's own and `plugins/` what the engine brings.
std::string builtGameDataDir();

} // namespace s3d