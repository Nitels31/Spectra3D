# Spectra3D

A lightweight 3D engine with a **built-in editor** in the spirit of Godot, written in
**C++17 + OpenGL 3.3 compatibility + GLFW + FreeType**. Runs on **Windows** and
**Linux**. Startup
goes through a **launcher**: you pick or create a project, and the editor opens. A
project may carry **AngelScript** scripts that run when you press Play.

Licensed under the [Apache License 2.0](LICENSE).

URL: 
[![Telegram Bot](https://img.shields.io/badge/Telegram_Bot-Spectra3D-blue?logo=telegram)](https://t.me/spectra3d_bot)

## Repository layout

```
CMakeLists.txt        the single source of truth for the build
ARCHITECTURE.md       layers, dependency rules, the defects that were fixed
build_windows.bat     a CMake wrapper
build_linux.sh        a CMake wrapper

src/                 implementation (.cpp), split by layer
  core/              Log, Str, Math, Project (Settings, ProjectLibrary, ProjectInfo)
  platform/          Window (GLFW + keyboard/mouse state + frame waiting),
                     Filesystem (removeTree and the system folder picker)
  physics/           RigidBody, Material, PhysicsWorld (broadphase, solver, sleeping)
  assets/            MeshRegistry, MeshImport, loaders/{glb,obj,ply,fbx}
  render/            SceneRenderer, UiRenderer, TextAtlas, PrimitiveCache, Camera
  ui/                Theme (palettes), UiContext
  scene/             BodyFactory, SceneSerializer
  i18n/              Localizer
  editor/            Editor, panels/, Picking, Gizmo, AssetBrowser
  script/            ScriptHost, Bindings, BindEngine, BindScene, BindCamera,
                   BindInput, BindString
  app/               Application, Launcher, FixedClock, main

include/             every engine header, same layer split + json/
  <layer>/*.hpp      Log.hpp, Camera.hpp, Editor.hpp, ... (all .cpp files write
                     #include "core/Log.hpp" - no ../)
  json/json.hpp      header-only nlohmann/json, target spectra_json

third_party/         glfw, freetype, angelscript, miniaudio,
                    models_lib/ (cgltf, rapidobj, miniply, ufbx),
                    stb/ (stb_image, stb_vorbis), json/
assets/              lang/*.lang, image/{icons,textures}/*, models/*.{glb,obj,ply,fbx},
                     sky.jpg, sounds/*.wav
tools/               glbinfo.cpp, fbxtest.cpp, scripttest.cpp, skygen.cpp,
                     soundgen.cpp - standalone
                     utilities for reading assets, checking the scripting
                     layer (46 checks), making the sky and making the test
                     sounds; ui_language_test.ps1 drives the real
                     window to check that a dropdown does not act on its own
                     opening click
build/               all output: spectra3d.exe, glfw3.dll, engine.log,
                     settings.ini, project/, a copy of assets/ (gitignored)
  lib/               the static libraries the executable links, incl.
                     libspectra_runtime.a - every engine layer below app/ and
                     editor/, which is the engine without an editor
```

The repository root `include/` goes on the include path, so the same header is
spelled the same way from any layer, from an editor panel and from `tools/`, and no
`#include` breaks when a file moves.

`ARCHITECTURE.md` describes the layers and the dependency rule. In short,
dependencies only point upward - `core` <- `platform` <- `physics` <- `assets` <-
`render`/`ui` <- `scene` <- `script` <- `editor` <- `app`; `i18n` depends only on
`core` + `platform`. There is no global state (apart from the log).

## Building

You need CMake >= 3.16 and a C++17 compiler (MSVC or MinGW-w64 g++).

The build tree is **outside the source tree**: CMake creates `build/`, it is in
`.gitignore`, and the binary lands in `build/spectra3d.exe`
(`CMAKE_RUNTIME_OUTPUT_DIRECTORY`).

Windows, MSVC (Developer PowerShell):

```bat
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
build\spectra3d.exe
```

Same thing through the wrapper (it only calls CMake):

```bat
build_windows.bat
build\spectra3d.exe
```

With the Visual Studio generator add `-G "Visual Studio 17 2022" -A x64`; then
`--config Release` is required and `CMAKE_BUILD_TYPE` is ignored.

Linux:

```sh
sudo apt install build-essential cmake libglfw3-dev libgl1-mesa-dev libasound2-dev
./build_linux.sh
./build/spectra3d
```

`libasound2-dev` is ALSA, and miniaudio's ALSA backend includes
`<alsa/asoundlib.h>`: the sound is not optional on this platform without giving
the engine a sound that cannot be played at all.

CMake options:

| Option | Default | Effect |
|---|---|---|
| `SPECTRA_SCRIPTING` | `ON` | builds `third_party/angelscript` and defines `SPECTRA_SCRIPTING=1`; `OFF` removes the scripting layer (the log then says `built without scripting`) |
| `SPECTRA_BUILD_TOOLS` | `OFF` | builds `glbinfo` / `fbxtest` / `scripttest` / `skygen` / `soundgen`; off by default so a normal build produces exactly one `.exe`. Turn on with `-DSPECTRA_BUILD_TOOLS=ON` |

**The first build is slow**: `third_party/angelscript/source/*.cpp` compiles into a
static library called `spectra_angelscript`. That is expected;
`-DSPECTRA_SCRIPTING=OFF` removes that time.

After the build, CMake copies `glfw3.dll` (Windows) and `assets/` into `build/`, so
the exe runs from any directory. Mutable files (`engine.log`, `settings.ini`,
`build/project/`) are written next to the binary: at startup
`fs::adoptAssetWorkingDir()` locates the exe directory and makes it the working
directory.

## Launcher and projects

The application is a **two-state machine**: `Launcher` (startup menu) and `Editor`.
The Launcher owns the window until a project is chosen; when `Launcher::update()`
returns `Result::OpenProject`, `Application::openProject()` switches to the editor.
There is no way back: `Application::closeProject()` exists but is never called -
leaving the editor means closing the window.

The menu layout follows the concept: **a tab strip on top, a sidebar on the left,
the project list in the rest**. Every row carries its own **Open** and **Delete**
buttons; there is no shared action bar, because the row you are looking at is the
one you mean.

```
+----------------------------------------------------------+
| Spectra3D | All | Favorites |  OpenGL ... | 2 projects [Create] |
+------------+---------------------------------------------+
| Projects   | Projects                     N projects      |
|            |  O [icon] MyForest  C:/.../MyForest  [Open] [Delete] |
|            |                                             |
| Account    |                                             |
|        [G] |                                             |
+-----------------------------------------------------+----+
| Esc - quit                                  Quit       |
+----------------------------------------------------------+
```

| Element | What it does |
|---|---|
| **All** / **Favorites** | list tabs; Favorites shows only starred projects |
| Ring at the left of a row |_star_: adds or removes a project from Favorites; the state is written into the manifest, so it travels with the project |
| **Open** | opens the project in the editor |
| **Delete** | asks for confirmation, deletes nothing immediately |
| **Projects** in the sidebar | back to the project list |
| **Account** | a local session: user, type, engine version, projects found, folder. Nothing is ever sent anywhere |
| G in the sidebar corner | settings |
| **Create** | the creation tab: name plus folder picker, right there |

The project path at the bottom of the sidebar is the folder the list is read from,
so an empty list always has an explanation.

### Deleting a project

**Delete** opens a confirmation modal:

```
Delete project
Delete project "MyProject"?
C:\__Spectra3D\build/project/MyProject
This cannot be undone.
                                     [Return] [Ok]
```

* **Ok** - deletes the project folder with all its contents (recursively) and
  removes it from the recents list.
* **Return** or **Esc** - does nothing.
* Closing the window (the X in the title bar) also deletes nothing: deletion can
  only happen after **Ok** is pressed.

The read-only attribute is cleared before deletion, so a project unpacked from a zip
or from git deletes too.

### Folder picker

**Browse...** opens the **system Explorer dialog** (`IFileDialog`, `FOS_PICKFOLDERS`)
- with tabs, network paths, and a path field. The built-in folder browser survives
only as a fallback for systems without such a dialog (Linux without GTK, for
example), and the row under the field says so when that happens.

The folder can be changed both in settings and on the project creation screen: if
the entered folder differs from the active one, an **Apply** button appears, so a
project does not silently land in the wrong place.

### Project files

```
<projectsRoot>/MyProject/
  project.spectra    manifest (see below)
  scene.txt          the scene document
  editorlayout.txt   saved editor workspace (written by the editor itself)
  scripts/           *.as - project scripts
  models/            folder for project models
```

`project.spectra` is a plain `key=value` file (comments start with `#`), always
written with the same key order:

```
# Spectra3D project manifest
name=MyForest
version=1
engine=Spectra3D 1.0.0
created=2026-10-03T18:03:49Z
modified=2026-10-03T18:03:49Z
language=en
gravity=2
cameraX=0.0000
cameraY=6.0000
cameraZ=34.0000
cameraYaw=0.0000
cameraPitch=0.0000
cameraFov=70.0000
favorite=0
```

`ProjectLibrary::create()` writes the manifest, `readManifest()` reads it, and
`Application::openProject()` takes gravity and the camera (position, yaw, pitch,
fov) from it and loads `scene.txt` if the file exists. **After creation nobody
rewrites the manifest** (`writeManifest` is no longer called anywhere), so the
camera and gravity keys stay exactly as `create()` set them. The one exception is
`favorite`: the ring in the list toggles it via `ProjectLibrary::setFavorite()`.

The project root defaults to `<exe dir>/project`, i.e. `build/project`
(`Settings::defaultProjectRoot()`). It is configurable in the Settings screen and
persisted in `settings.ini` next to the binary:

```
# Spectra3D settings
projectRoot=C:\...\build\project
language=uk
theme=dark
frameRateCap=60
appIcon=icon.ico
recent=C:\...\build\project\MyForest
```

`recent` holds up to 8 absolute paths, newest first. `appIcon` is a bare file name
inside `assets/image/icons`, chosen in the settings screen and applied to the window
straight away.

The project's `models/` folder is created together with the project, but the
editor does not scan it: `AssetBrowser::refresh()` reads `assets/models` of the
shared asset root (falling back to the working directory), and the list shows in
the **Create** menu. Models are added to the scene from the menu or from a script
via `engine::importModel`.

### Languages

Twenty-three bundles live in `assets/lang/`.

**Six are real languages**: **English**, **Українська**, **Deutsch**, **Français**,
**Italiano** and **Español** (`{en,uk,de,fr,it,es}.lang`). The switcher in Settings
applies the language immediately and writes the choice to `settings.ini`; the
editor's top bar has no language switch. Languages are named by themselves
(Deutsch, Français, Italiano, Español) rather than by translation, because that is
how people look for them.

**Seventeen are jokes**, and they are jokes rather than bad translations:

| Code | Language | What it does |
|---|---|---|
| `lol` | Lolcat | a cat talking at you in the first person: "I can haz", "Yu", "meow", "purr" |
| `jeb` | Jeb Kerman | the KSP mascot and his hangar: you do not press "Open", you press "Aboard" |
| `pir` | Pirate | every label narrated by a buccaneer, "arr" included |
| `u8f` | Broken UTF-8 | genuinely invalid bytes and lone `?` glyphs, appended byte-wise because a text round-trip would repair it into something boring |
| `hex` | Hexadecimal | the whole UI as hex, kept mechanical on purpose |
| `gd` | Geometry Dash | attempts, cubes, and the phrase "so hard" |
| `mc` | Minecraft | blocks, crafting, and the Nether |
| `ut` | Undertale | shouts SAVE, offers MERCY, and labels one button `SAVE` on purpose |
| `cel` | Celeste | strawberries, the summit, and climbing again |
| `dm` | Doom | a marine who writes the wad and offers no warranty |
| `wows` | Wolf of Wall Street | diamond hands, stonks, and lambos |
| `sus` | Among Us | vents, tasks, and everything being suspicious |
| `leet` | 1337 | `h3ll0 w0rld`, `3v3ry k3y`, `1337 5p34k` |
| `tlh` | Klingon | `Qapla'`, `nuqneH`, `tIn` |
| `gop` | Пацан | Russian gopnik: "брат", "жиза", "кринж", "база" |
| `sha` | Shakespeare | "thee", "hath", "'tis", and a playbill instead of a menu |
| `pt` | Português | a seventh real language, for completeness |

A joke bundle has **no untranslated English left outside the `help.*` section**, and
no Russian in it unless it is the Russian one. The Create menu is included: the
seven primitive names used to be hardcoded English in C++, and are now
`shape.box` through `shape.prism` like any other string.

`help.*` is left in English on purpose, in **every** language including Russian.
Keyboard names are `W A S D` everywhere on earth, and a half-translated help
screen teaches nobody anything.

All 23 files carry the same 236 keys. A key missing from a bundle shows up as the
key itself rather than as emptiness. The text atlas alphabet is ASCII, Cyrillic and
**Latin-1** (U+00C0..U+00FF plus `« »`, `œ Œ`, `¡ ¿`): without it, German umlauts,
French accents and Spanish `ñ` would render as empty boxes.

`hex` and `u8f` deliberately have no `%d` in their log lines - a broken bundle that
also miscounts a format string is a crash, not a joke.

## Scripting (AngelScript)

The runtime is **AngelScript 2.38.0** (`third_party/angelscript`), layer
`src/script`. Project scripts live in `<project>/scripts/*.as` and run **when Play is
pressed**: `Application::runProjectScripts()` takes every `.as` in the directory (by
name) and calls `script::Host::runFile()` for each. Each file is a separate
AngelScript module, so an error in one does not destroy another's functions.
Compile and runtime errors go to `engine.log` (the sink comes from
`Application::initialize()`); `engine::log*` writes to the same log with a `[script]`
prefix.

### Entry point

Each script runs **exactly one** entry point:

1. a function named **`onInit`**, **`main`** or **`start`** (case does not matter);
2. if there is none - the **first** global `void()` in declaration order;
3. if every function is marked `shared` - nothing runs, the script just compiles.

A helper `void makeForest()` next to `onInit` therefore **does not** run on its
own - the entry point calls it (otherwise the scene is built twice). `shared`
removes a function from the entry point rule entirely.

Note what "first `void()`" means: **no parameters**. A hook that takes `dt` is not an
entry point, so `void onUpdate(float dt)` never runs on load - only from the frame
loop. (`void onStop()` has no parameters, so a script that declares *only* `onStop`
still runs it on load. That is the old rule unchanged.)

### Hooks

Four optional declarations. All of them are optional, and a script that declares none
of them is completely normal:

| Hook | When it runs | `dt` means |
|---|---|---|
| `void onInit()` | once, when the module is executed (see the rule above) | - |
| `void onUpdate(float dt)` | **once per rendered frame**, before physics | the real frame time |
| `void onFixedUpdate(float dt)` | **once per physics step**, inside the accumulator | always the fixed step |
| `void onStop()` | once, when Play finishes, after the scene is restored | - |
| `void onUpdate()` | legacy: every frame, **only if no `dt` form exists anywhere** | - |

The two update hooks exist because the difference matters. `onUpdate` is driven by the
display, so `position += speed * dt` moves at the same visual speed on any monitor but
does **not** simulate the same physics. `onFixedUpdate` is driven by the fixed
accumulator, so it always advances by exactly 1/60 s: use it for anything that should
behave identically on a 60 Hz laptop and a 144 Hz desktop. It runs at most twice per
frame - see the step budget below.

If two scripts declare the same hook, the first one loaded is the one that gets called
and the engine logs a warning saying so, rather than silently dropping the other.

**Step budget:** the accumulator runs at 60 Hz with a cap of **2 catch-up steps per
frame**. A frame longer than 100 ms is capped by `kMaxFrameTime` anyway, so the loop
can never spiral; long stalls slow time down instead. (The previous cap of 10 was
unreachable — 0.1 s of stall is only 6 steps — so it just looked unbounded.)

### Input

Two ways to ask, and the difference is the point.

**State** - "is this held right now". Good for movement, for anything that runs every
frame regardless:

| Call | What it does |
|---|---|
| `bool isKeyDown(int key)` | is a key held, `engine::KeyW`, `KeyA`, `KeyS`, `KeyD`, ... |
| `bool isMouseButtonDown(int button)` | `engine::MouseLeft`, `MouseRight`, `MouseMiddle` |
| `bool getMouseDelta(float &out dx, float &out dy)` | movement since the previous frame, in pixels |
| `void setMouseCaptured(bool)` | confine the cursor to the window |
| `bool isMouseCaptured()` | current state |

**Events** - "what happened, in order, this frame". Walk the queue:

| Call | What it does |
|---|---|
| `int eventCount()` | how many events arrived this frame |
| `int eventType(int index)` | `EventKey`, `EventKeyUp`, `EventMouse`, `EventMouseUp`, `EventScroll` |
| `int eventCode(int index)` | the key (`KeyW`) or the button (`MouseLeft`); `-1` for scroll |
| `double eventValue(int index)` | scroll ticks; `0` for everything else |

```angelscript
void onUpdate(float dt)
{
    for (int i = 0; i < engine::eventCount(); ++i)
    {
        if (engine::eventType(i) == engine::EventKey &&
            engine::eventCode(i) == engine::KeySpace)
            jump();
    }
}
```

Why both: a key **tapped** and released inside one frame is state nobody can see -
by the time `isKeyDown` is asked, it is already up, and holding it for a frame is a
different thing from pressing it. That is the whole reason the queue exists. If your
script only cares whether a key is held, use `isKeyDown` and skip the loop.

Rules worth knowing:

- **The queue is not consumed.** Nothing a script does takes an event away, so two
  scripts both see everything. It is cleared once per frame, at the end of the frame
  that had its chance to read it.
- **A script sees everything, including what the editor already handled.** The queue
  is filled in the window callbacks, separately from the editor's own edge flags, so a
  shortcut the editor acted on is still delivered to the script.
- **Reading it costs nothing if you do not.** `eventCount()` is a size, not a scan.
- **Mouse movement is not an event.** One pixel of movement is one callback, so the
  queue would grow with distance instead of with intent. `getMouseDelta()` is the
  aggregate.
- **One reader for the delta and the scroll.** Both are consuming: the editor gets
  them first, so a script typically sees `0`. That is a known gap, not a bug you
  introduced.

Headless hosts - `scripttest`, anything built without a window - answer `false`,
return `0` events, and zero their `out` arguments instead of dereferencing null.

### API

The whole API lives in the `engine` namespace (the host does
`SetDefaultNamespace("engine")`), so a script writes `engine::log`,
`engine::ShapeBox` and so on. All bindings mutate the live `PhysicsWorld` in
place - which is why the same script makes sense both while editing and while
playing.

**Logging:** `void log(const string &in)`, `logWarning`, `logError`.

**Body creation** (returns an index, `-1` on failure; the trailing `density` is
optional, default `1.0`; values <= 0 become `1.0` with a warning):

| Call | What it does |
|---|---|
| `int spawnBox(float x, float y, float z, float sx, float sy, float sz, float density)` | a box of size `sx x sy x sz` (full dimensions) |
| `int spawnSphere(float x, float y, float z, float radius, float density)` | a sphere of the given radius |
| `int spawnShape(int shape, float x, float y, float z, float sx, float sy, float sz, float density)` | any figure from `Shape`; `ShapeMesh` is rejected - for a model there is `importModel` |

**Gravity:** `void setGravity(int axis)`, `int getGravity()`; enum `Gravity` -
`GravityNegX`, `GravityPosX`, `GravityNegY`, `GravityPosY`, `GravityNegZ`,
`GravityPosZ` (values = axis numbers, `GravityNegY` = 2).

**Bodies** (take an index; out of range logs one line and does nothing):
`getBodyX/Y/Z`, `setBodyPosition(index,x,y,z)`, `setBodyVelocity(index,x,y,z)`,
`getBodyMass`, `setBodyMass(index,m)`, `getBodySpeed`, `getBodyShape`,
`wakeBody(index)`.

**Scene:** `int getBodyCount()`, `void clearScene()` (removes every body and
group), `int importModel(const string &in path, float x, float y, float z)`,
`int removeBody(int index)`. `importModel` hands back an OS path, so a relative one
counts from the binary directory (`build/`) and an absolute one is taken as-is; the
model becomes a body with a box hull (mass from its dimensions).

**Groups** - a bag of indices: `int createGroup(const string &in name)`
(idempotent by name), `void addToGroup(int group, int body)`,
`int getGroupSize(int group)`, `void setGroupPosition(int group, float x, float
y, float z)` (moves the centroid to the point; calling it again with the same point
is a no-op). `removeBody` keeps groups consistent.

**Camera:** `void setCamera(float x, float y, float z, float yaw, float pitch)`
(radians, pitch clamped to +-(pi/2 - 0.001)), `void setCameraFov(float deg)`
(clamped to 10...170).

**Sound** - the names of the sounds in `assets/sounds/` (the name of a sound is
the name of its file without the extension). Every one of them answers `false`
on a machine with no sound card, and nothing else changes:

| Call | What it does |
|---|---|
| `bool playSound(const string &in name)` | plays it once |
| `bool playSoundLoop(const string &in name)` | plays it until it is stopped |
| `bool playSoundWith(const string &in name, float volume, float pitch)` | plays it once, louder or faster |
| `void stopSound(const string &in name)`, `void stopAllSounds()` | silence |
| `bool isSoundPlaying(const string &in name)`, `bool hasSound(const string &in name)` | what is sounding, what exists |
| `void setMasterVolume(float volume)`, `float getMasterVolume()` | the loudness of everything, 0..1 |
| `bool loadSound(const string &in name, const string &in path)` | loads a sound from a file, under a name |
| `void setBodySound(int index, const string &in name)`, `bool bodyHasSound(int index)` | the sound a body makes when it hits something |

**Types:** enum `Shape` - `ShapeBox`, `ShapeSphere`, `ShapeCylinder`,
`ShapeCapsule`, `ShapeCone`, `ShapePyramid`, `ShapePrism`, `ShapeMesh`; the type
`string` is registered in the global namespace (inside `engine::` it is also
available without the prefix) with `+`, `==` / `!=`, `length()`, `setLength(uint)`.

What the API does not have: static/visible/locked flags, materials, colors, and
mesh lookup by name - `importModel` returns a finished body, not a registry entry.

### Example

`build/project/MyProject/scripts/pinball.as`:

```angelscript
// Entry point - onInit. Everything else is a helper, which the host
// never runs on its own.

void buildArena()
{
    engine::clearScene();
    engine::setGravity(engine::GravityNegY);

    // floor: a thick box
    engine::spawnShape(engine::ShapeBox, 0.0f, -1.0f, 0.0f, 40.0f, 2.0f, 40.0f, 1.0);

    // walls
    engine::spawnShape(engine::ShapeBox,  0.0f, 3.0f, -10.0f, 40.0f, 8.0f,  1.0f, 1.0);
    engine::spawnShape(engine::ShapeBox,  0.0f, 3.0f,  10.0f, 40.0f, 8.0f,  1.0f, 1.0);
    engine::spawnShape(engine::ShapeBox, -10.0f, 3.0f, 0.0f,  1.0f, 8.0f, 20.0f, 1.0);
    engine::spawnShape(engine::ShapeBox,  10.0f, 3.0f, 0.0f,  1.0f, 8.0f, 20.0f, 1.0);
}

void dropBalls()
{
    int balls = engine::createGroup("balls");
    for (int i = 0; i < 5; ++i)
    {
        float x = -8.0f + i * 4.0f;
        int ball = engine::spawnSphere(x, 14.0f, 0.0f, 1.0f, 1.0);
        engine::addToGroup(balls, ball);
        engine::setBodyVelocity(ball, 0.0f, 0.0f, -8.0f);
    }
    engine::log("pinball: arena ready");
}

void onInit()
{
    engine::setCamera(0.0f, 10.0f, 26.0f, 0.0f, -0.3f);
    engine::setCameraFov(70.0f);
    buildArena();
    dropBalls();
}
```

The minimal script (`scripts/hello.as`):

```angelscript
void onInit()
{
    engine::log("hello from MyProject");
    engine::spawnBox(0.0f, 5.0f, 0.0f, 2.0f, 2.0f, 2.0f, 1.0);
}
```

And something that moves, showing the two update hooks side by side:

```angelscript
int spinner;

void onInit()
{
    spinner = engine::spawnBox(0.0f, 5.0f, 0.0f, 1.0f, 1.0f, 1.0f);
}

// Driven by the display: reads the keyboard, so it wants the real frame time.
void onUpdate(float dt)
{
    if (engine::isKeyDown(engine::KeySpace))
        engine::addImpulse(spinner, 0.0f, 40.0f, 0.0f);
}

// Driven by the fixed step: same result on every machine.
void onFixedUpdate(float dt)
{
    engine::setGravity(0.0f, -9.81f, 0.0f);
}
```

### Checking it

The entry point rule and the hooks are testable without a window and without a
project. The tools are not built by default, so first:

```bat
cmake -S . -B build -DSPECTRA_BUILD_TOOLS=ON
cmake --build build --config Release
build\scripttest.exe
```

120 checks: the entry-point rule and its interaction with hooks, a `dt` hook that is
not an entry point, `onUpdate` / `onFixedUpdate` / `onStop` each called only on their
own turn, a duplicate hook declaration reported as a warning, a hook that raises an
exception mid-flight followed by a hook that still runs, the function cache dropping
both its hits and its misses when a module is replaced, the input bindings with no
window, the event queue's ordering and cap, and a compile error as an ERROR
diagnostic.

## Features

### Launcher

- **Tabs and sidebar** per the concept: `All` / `Favorites`, projects and the
  account record on the left, the settings gear at the bottom; on the right a
  counter and `Create`.
- **Project row**: a ring-star, an icon, the name, the full path, `Open` and
  `Delete`. Clicking the row selects it, `Enter` opens it.
- **Favorites** are stored in the project manifest, not in `settings.ini`, so they
  survive moving the folder.
- **Deletion** only through a confirmation; `Esc` and closing the window delete
  nothing.
- **Project creation** on its own tab, with a folder field and the system Explorer
  dialog next to it.
- **The account record** is honestly local: no sign-in, no sync.
- **Settings**: the projects folder, Apply / Reset, language, Rescan, theme, the
  frame rate limit and the application icon. Everything that has more than a couple
  of values is a dropdown rather than a row of buttons, so the page stays readable
  and the current answer is visible without counting columns.

### Themes

Three palettes, switching instantly, the choice written to `settings.ini`
(`theme=dark|light|contrast`):

| Theme | When |
|---|---|
| **Dark** | the default |
| **Light** | a bright room: a dark UI there is a lamp in the face |
| **Contrast** | black background, pure white text: a laptop on battery outdoors |

The palette is plain colours in `theme::`, not compile-time constants: switching has
to be visible to every panel, and there are hundreds of such references in the UI.
Only `theme::setActiveTheme()` may write.

### Frame rate and heat

Vsync alone does **not** cap frames: on this machine the driver reported ~154 FPS on
a static menu, i.e. the GPU was drawing 154 frames a second for a still image. So
there is an explicit cap: **30 / 60 / 120 / Uncapped** (`frameRateCap` in
`settings.ini`, default 60) - as a list, not a row of buttons.

The cap is implemented as an **absolute schedule**: the next frame must happen at
`nextFrameTime_`, not "one budget after the previous frame ended". Adding a budget
to a timestamp that already contains the draw time makes the period longer than
asked every single time - a cap of 60 silently became 42.

Frame waiting is precise: `Sleep()` on Windows rounds up to 15.6 ms, which would
turn a cap of 60 into 30, so the loop waits on a waitable timer with
`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`. Verified: 30 -> 30 FPS, 60 -> 60 FPS.

### Draw only when there is something to show

A window nobody touches **draws nothing**. A frame is needed only when:

* the user did something - keyboard, mouse, scroll, text, window resize;
* someone is **holding** a key or button - camera flight, gizmo drag;
* physics is simulating (Play mode);
* a one-second tick has passed - it gives a "stuck" frame visibility once a second.

Between those events the loop simply waits. Measured on this machine:

| State | CPU over 6 seconds |
|---|---|
| Menu, nothing happening | **0.00 s** |
| Editor, nothing happening | **0.00 s** |
| Play, 64 bodies | 0.80 s |
| After Stop | **0.00 s** |

Before this, the static menu ran at 154 frames a second - which was the cause of
the laptop heating up.

Three details that matter:

* **Waiting must be sleep, not polling.** `WaitForSingleObject(timer, 0)` does not
  sleep at all - the first version of this mechanism ate 95% of a core while idle.
  The loop now waits on `MsgWaitForMultipleObjects`: either the frame time is near,
  or a message arrives - so a click is handled instantly and the core sleeps.
* **An event lives exactly one frame.** At the start of every iteration
  `Window::dropUnreadInput()` drops the presses, clicks, text and scrolls nobody
  read; held keys stay, because "someone is holding it" is exactly the condition.
  Without this, a single `Enter` used to open a project would keep the loop
  "busy" forever and the editor would draw at frame rate while drawing nothing new:
  measured **0.9 s CPU over 8 s** on an idle cooling editor versus **0.06 s** after
  this step.
* **Waiting must wake on an event, not run out the clock.** During a pause until
  the one-second tick, the loop waits on `waitForFrame(window, tickDue, true)`,
  which returns only once `Window::peekActivity()` saw movement. This is not a
  trifle: waiting used to run out to the end of the tick, so hovering a button lit
  up **a second later**. `peekActivity()` is separate from `consumeActivity()`
  precisely for this: the first reads an event without latching the cursor
  position, otherwise the movement that woke the wait gets eaten and the loop
  cannot see that something happened. There is no waking between frames (`false`):
  that frame arrives anyway, so the mouse should not raise the frequency.
* **Exactly one entry point** - see the scripting section.

### OpenGL

The engine asks for **OpenGL 3.3 in the compatibility profile** (not core: the
renderer is written against the fixed pipeline - `glOrtho`,
`glEnableClientState`, `glTexCoord2f`, all of which core removes). If the driver
cannot give 3.3 compatibility, it falls back to 2.1 and logs it.

**The version the interface shows is the requested one, not what the driver
reports.** NVIDIA, and others, print the highest version supported for *any*
compatibility context into `GL_VERSION`, so a 3.3 context on this laptop reports
"4.6.0 NVIDIA 391.35". So the version is stated on the launcher startup and on the
About page, while the full driver string goes to the startup log.

### Editor (layout from the drawn concept)

```
+------------------------------------------------------------+
| File Edit View Create Help | o - = hand | play pause stop | 0 / 3 ? [search] Layers reload |
+---------------+-------------------------+---------------------------------+
| Hierarchy  *  | Scene       grid up ... fov 70 | Inspector                       |
| * [search]    +--------------------------+ [filter]                          |
|  Scene root   | |                      | | Transform / Shape / Material     |
| * Box 00      | |      viewport        | | Flags / Anchor / State / Node    |
| * Sphere 01   | |  [ select ]          | |                                  |
|               | |  [ move ]            | |                                  |
|               | |  [ rotate ]           | |                                  |
|               | |  [ scale ]            | |                                  |
|               | |  [ pan ]              | |                                  |
|               +--------------------------+---------------------------------+
| Console       | warn 1  err 0  info 18                                        |
+---------------+-----------------------------------------------------------------+
```

Panels, top to bottom:

* **Top bar (toolbar)** - five menus on the left, then five tool buttons (select,
  move, rotate, scale, pan - the same set as in the viewport column), the
  **play pause stop** buttons in the middle, then on the right the `0 / 3` counter
  (selected / bodies), **`?`** for help, a **command search** field, **Layers**
  and **reload** (re-read the scene from disk). All five menus work: **File**
  (save / load / reload / quit), **Edit** (duplicate, delete, clear selection,
  wake), **View** (grid, vectors, frame selection, reset camera, which panels are
  open), **Create** (seven primitives and the models from `assets/models`) and
  **Help** (keyboard and mouse, about). The search in the bar finds a menu row by
  substring and runs it on `Enter`; `Esc` or a click outside closes both the panel
  and the text set. There is **no FOV in the editor** - it is a property of the
  game camera, and the editor camera has one job: look at the scene.
  `Camera::fovDeg`, `cameraFov` in the manifest and `engine::setCameraFov` for the
  game all remain.
* **View toolbar in the viewport** (the "Scene" strip) - a row of icon buttons on
  the right: grid, vectors, **snap** (a magnet: dragging becomes a multiple of
  0.25), frame selection, reset camera, the "Layers" panel. Every button performs
  its command.
* **Movable panels** - hierarchy, inspector, console, favorites and sounds sit in six
  places: the left column, the right column, the bottom strip, the two bottom
  corners, and free float. Each can be dragged by its title to another place
  (panels swap) or into the middle of the scene - then it becomes floating, with a
  border and a shadow, above the rest. The inner edge of a docked panel drags to
  change width or height; a floating one resizes from any of four edges and any
  corner. The layout is always recomputed from which columns and rows are
  occupied: an empty edge
  gives its space to the scene. Sizes, zones, floating panels and closed panels
  are remembered in `<project>/editorlayout.txt`, so a project opens the way you
  left it.
* **Every panel has a place of its own** - the hierarchy on the left, the
  inspector on the right, the console along the bottom, the **favorites in the
  bottom-left corner** and the **sounds in the bottom-right one**. That is
  Unity's arrangement: a panel that has no place is a panel nobody opens, and a
  corner with nothing in it is a hole in the window.
* **Hierarchy** - a tree of objects with search (filtered by visible name). The
  star on each row puts an object into favorites; the star in the header leaves
  only favorites. Favorites are saved in `scene.txt` together with the object.
* **Favorites** - a panel in the bottom-left corner: a list of stars, the same
  gesture in the same column; clicking a row selects the object, clicking the
  star removes it from favorites. Clicking the panel lets you drag it to an edge
  or into the scene.
* **Sounds** - a panel in the bottom-right corner: the sounds the engine has
  loaded, the device they play through, and two buttons per row - one plays the
  sound, the other gives it to the object that is selected. See
  [Sound](#sound).
* **Inspector** - a property filter and the sections Transform / Shape / Material /
  Flags / Sound / Anchor / State / Node; the body never leaves the panel's bounds
  (scissor) and scrolls with the mouse wheel; on the right, a scrollbar when there
  are more sections than room. The wheel belongs to the panel under the cursor.
* **Console** - filters by level with live counters: `Warnings 1`, `Errors 0`,
  `Info 18`. An empty console is normal.
* **Layers** - a panel under the button in the bar: every body in a list, with
  visibility and lock toggles. A locked body cannot be grabbed by the gizmo and is
  not deleted with `Delete`, so a lock means something rather than being just a
  flag in a file.

Every zone of the screen is a separate file in `src/editor/panels/`: `TopBar.cpp`
(the bar itself), `TopBarOverlay.cpp` (all the panels that drop from it),
`LeftColumn.cpp`, `FavoritesDock.cpp`, `SoundsDock.cpp`, `ScenePanel.cpp`,
`InspectorDock.cpp`, `BottomBar.cpp`.

The window title is the icon, `Spectra3D`, a dash and the project name: the icon
comes from `assets/image/icons`, chosen in the launcher's settings and remembered in
`settings.ini` (`Window::applyIcon`), CMake embeds the default one in the `.exe` via a
resource (`windres` / `rc.exe`, if a resource compiler exists), and when the file is
missing it is drawn in code. There is no frame counter in the title: the concept does
not have one, and a title that changed every second would also have to be rewritten
during that same tick.

### Editor (capabilities)

- **Two modes**: **Create** (physics frozen) and **Play** (physics running). They
  switch **only via the play / pause / stop buttons** in the menu bar - there are no
  hotkeys for it. Entering Play saves a snapshot of the scene, and STOP restores
  the scene to its editing state; project scripts run on that transition.
- **There are no scene save/load buttons in the editor toolbar.** `scene.txt` is
  read when the project opens and nobody writes it afterwards - see "Controls".
- **Primitives**: Box, Sphere, Cylinder, Capsule, Cone, Pyramid, Prism. The
  **Create** menu, plus model import from the same place.
- **Model import of `.glb` / `.gltf` / `.obj` / `.ply` / `.fbx`**:
  - `.glb/.gltf` via cgltf - POSITION/NORMAL, indices, baseColorFactor, node
    hierarchy, auto-normals
  - `.obj` via rapidobj - triangulation, normals/flat shading, colours from .mtl
  - `.ply` via miniply - binary and ASCII, normals optional
  - `.fbx` via ufbx - binary and ASCII, hierarchy, auto-normals, unit conversion
    to metres, discarding gigantic parts
  - the **Create** menu lists **all** models in `assets/models`, with no truncation;
    the list is refreshed by the reload button in the bar or
    **File -> Reload from disk**. A menu longer than the window is bounded by the
    window and scrolls, and shows at the bottom how many rows are below. Physics is
    a box hull with auto-normalized size; meshes are stored in `scene.txt` by name
    and reloaded
- **Toolbar**: File / Edit / View / Create / Help - all five open lists; the
  play/pause/stop buttons in the middle; on the right the "selected / bodies"
  counter, `?`, command search, Layers and reload. There is no language switch in
  the bar - the language stays in the launcher's settings; the `GL 3.3` label was
  removed too, the full driver string is in the log anyway.
- **Left column**: **Hierarchy** - a tree of objects (scene root -> type + index)
  with search and favorite stars. The star in a row puts an object into favorites,
  the star in the header leaves only favorites; both write into `scene.txt`, so
  favorites survive a restart.
- **The Favorites panel**: a list of objects with a star - click a row to select,
  click the star to remove from favorites. Closed by default (**View ->
  Favorites**), and just another panel like the other three: draggable by its
  title, resizable by edges and corners, hidden with **X**.
- **Movable panels**: hierarchy, inspector, console and favorites can be dragged by
  their title to another zone - the left column, the right column, the bottom strip,
  either bottom corner, or free float over the scene (panels swap places in the
  docked zones) - then the panel floats, with a border and a shadow, above the
  rest. The corners are their own zones rather than part of a side column, so
  favorites have a home beside the console instead of leaving a hole under it. The
  inner edge of a docked panel drags to change width or height, a floating one
  resizes from any of four edges and any corner. A free zone gives its space to the
  scene rather than leaving an empty dock.
- **The workspace is remembered**: `<project>/editorlayout.txt` holds the edge
  sizes, the zone and the rectangle of each panel, and which of them are closed. The
  file is in the project, not in `settings.ini`: two projects, two layouts, and
  moving a folder moves its layout with it. Values from the file are not trusted:
  a size outside the bounds is clamped, unknown keys are ignored, and two panels on
  one edge (only possible in a hand-edited file) let the later one float.
- **The Inspector panel**: a filter field, sections Transform / Shape / Material /
  Flags / Anchor / State / Node. Numeric fields support drag-editing and keyboard
  entry; buttons Wake / Delete. The inspector's body never leaves the panel's
  bounds (scissor) and scrolls with the mouse wheel; on the right, a scrollbar when
  there are more sections than room. The wheel belongs to the panel under the
  cursor: over the hierarchy the tree, over the inspector the properties.
- **The Layers panel** (from the bar button or the view toolbar): a list of bodies
  with visibility and lock toggles; clicking the name selects the body.
- **Viewport**: the view toolbar in the "Scene" strip (grid, vectors, snap, frame
  selection, reset camera, "Layers"), a vertical column of tools on the left,
  navigation axis balls on the right (click = align the camera), ray-pick.
- **The grid has no edge**: it is a window on the world grid that follows the
  camera - the patch is snapped to the major step and moved with the camera, so
  every line lands back on the world position it was on before. The minor lines
  are one unit apart and fade out near the camera, the major lines are ten units
  apart and carry the grid to the horizon, where one unit is smaller than a
  pixel and would only alias. Every line is cut into short segments so that
  each of them can fade along its own length. The world axes stay at the origin
  and fade with the distance to it.
- **Rotation** by gizmo ring follows the **world** axes (`gizmo::axesFor` returns
  world axes for Rotate, not the body's) - so the ring does not turn with the body,
  and the angle is computed **on screen**, around the projection of the body's
  centre, adding the shortest step (`std::remainder`) each frame. Dragging across
  the ring's body therefore does not spin the body 360 degrees, a ray parallel to
  the ring's plane does not jerk it, and a slow drag rotates exactly like a fast
  one.
- **Snap**: the magnet in the view toolbar - gizmo dragging becomes a multiple of
  0.25 on all three axes, not only on the one you are pulling.
- **Gizmo**: MOV - arrows with cones, dragged along the world axes; SCL - cube
  handles on the body's **local axes**; multiplicative scaling (Box - SX/SY/SZ,
  Sphere - radius, Mesh - uniform SCALE). Handle picking uses screen distance.
- **Bottom panel**: the Console with level filters and counters; drags to the
  bottom edge of the window. There is no status line.
- **Sound**: the Sounds panel lists what the engine has loaded and plays it; the
  `+` button gives a sound to the selected object, which then makes it when it
  hits something - louder the harder the hit. A hit is an impulse, not a
  contact: a body resting on the floor carries its own weight every step, and
  three times that is where "it was hit" becomes "it is sitting there".
- **Help** (`?` in the bar or the **Help** menu): keyboard and mouse in two
  columns and an About page (version, OpenGL, scene file, language).
- **Logs**: levels INFO/WARN/ERROR, output to stderr, the file `build/engine.log`
  and the Console panel; startup diagnostics (OpenGL version, GPU, font status,
  script runtime version, project root, body count).

### Details worth noting

- **Window title**: the icon, `Spectra3D`, a dash, the project name. No frame
  counter - see "Draw only when there is something to show".
- **Icon**: one of the `.ico` files in `assets/image/icons`, pickable in the
  launcher's settings and applied to the window and the task bar immediately; the
  default is also embedded in the `.exe` itself, and if the file is missing the
  editor draws a prism in code.
- **Text is not truncated with "...", it shrinks.** If a string does not fit, the
  font is squeezed (`UiRenderer::textScaled`) down to 0.6, and only then are
  characters dropped. Button labels are measured: `Uncapped` is 60 pixels wider
  than `Off` at 60 pixels, and the same button cannot have one width for both.
- **6-direction gravity** - keys `1..6`.
- **Rigid body physics**: OBB and Sphere (all primitives with their own volumes and
  moments of inertia); integration at a fixed 60 Hz step (`FixedClock`, up to 10
  steps per frame); body-body collisions (Sphere-Sphere, Sphere-Box, Box-Box) and
  body-wall; sequential impulses with early exit, friction, warm starting,
  penetration correction, speculative contacts (resting stability), and body
  sleeping.
- **Spatial hash broadphase** (sort-based, no allocations): 2 m cells, pairs
  deduplicated by uint64 keys -> O(n) instead of O(n^2).
- **CPU frustum, no glGet** - the P*MV matrices are computed on the CPU, culling
  without synchronous queries to the driver.
- **Display lists** for static geometry: unit primitives, each wall's grid, arena
  edges - baked once.
- **A sky and a ground without an edge**: the sky is a picture - `assets/sky.jpg`,
  an equirectangular panorama made by `tools/skygen.cpp` - drawn as six faces on
  a cube around the camera, and the grid is a window on the world grid that
  follows the camera: fine lines near it, coarse ones far away, dissolving into
  the horizon instead of stopping at a boundary.
- **UI vertex batching**: every quad/circle/glyph of a frame is reduced to two
  buffers and drawn with two `glDrawArrays`.
- **Text**: FreeType -> a texture atlas of ASCII + Cyrillic from the system TTF
  (Consolas/Arial/Segoe UI/Tahoma on Windows, DejaVu/Liberation on Linux), alpha
  blending, width estimation as a fallback.

## Controls

**There are no hotkeys in the editor that change the scene.** Play / Pause / Stop,
saving, loading and deleting are buttons in the interface only; this is a deliberate
choice and the keys should not come back. The reason is practical: STOP restores
the snapshot taken when Play began, so everything built during the run disappears;
DEL and scene loading destroyed work immediately. The keys that remain destroy
nothing:

| Key / mouse | Action |
|---|---|
| LMB | select an object / drag the gizmo / drag and resize panels / UI |
| RMB (hold) | look around with the camera |
| `W A S D`, `SPACE`/`C` | camera flight (+`SHIFT` to boost) |
| `1..6` | gravity direction `-Y +Y -X +X -Z +Z` |
| `Esc` | close a panel hanging from the top bar and clear the search |
| `Enter` | run the first command search match |

The camera and gravity only have power in the editor - in the launcher these keys
do nothing. `Editor::handleHotkeys()` (`src/editor/Editor.cpp`) is deliberately
empty: Play mode, saving, loading and deleting live on buttons and menu items only.

In the launcher (`Launcher::handleKeys`):

| Key | Action |
|---|---|
| `Up` / `Down` | move the project selection (while focus is not in a text field) |
| `Enter` | create the project if focus is in the name field; apply the root if focus is in the path field; otherwise open the selected project |
| `Esc` | close the folder picker modal or quit the program |
| mouse wheel | scroll the project list |

Scene saving and loading: `Editor::saveScene()` / `Editor::loadScene()` live in the
**File** menu (Save / Load / Reload from disk) and write to the current project's
`scene.json`.

**Save As** asks *where* the scene goes, not what it is called: the platform's
own folder chooser opens, and the name stays what it was. A name typed into a
dialog is a spelling test; a folder chosen in Explorer is a place.

## Scene format

`scene.json` in the project directory is a JSON document: named fields, named
shapes, and a position on one line rather than four. It is the only format a
scene is written in.

```json
{
    "format": "spectra3d-scene",
    "version": 1,
    "gravity": "negY",
    "bodies": [
        {
            "shape": "box",
            "position": [1, 2, 3],
            "velocity": [0, 0, 0],
            "angularVelocity": [0, 0, 0],
            "orientation": [1, 0, 0, 0],
            "halfExtents": [0.5, 0.5, 0.5],
            "mass": 1,
            "material": { "friction": 0.5, "restitution": 0.3, "density": 1, "isStatic": false },
            "flags": { "visible": true, "locked": false, "favorite": false },
            "sound": { "name": "thud", "volume": 0.5 }
        }
    ]
}
```

A shape is one of `box`, `sphere`, `cylinder`, `capsule`, `cone`, `pyramid`,
`prism`, `mesh`, `camera`. A `mesh` body carries a `mesh` object with the
model's name and its scale; a `camera` carries a `camera` object with its field
of view and whether it is the one the game looks through. Meshes are looked up
by name in `assets/models`, then in the working directory, so a scene survives a
restart. A body that makes a sound carries a `sound` object with the name of
the sound and how loud it is; a body without one has no `sound` object at all.

The project manifest names the file its scene is in (`scene=scene.json`), so a
project can keep its scene somewhere else.

## Skybox

The sky the world is seen against is a cube: six faces, drawn on a cube around
the camera. It comes from a file - the engine makes no sky of its own, and a
scene with no skybox has no sky.

There are two ways to give it one:

- **A picture.** `sky.jpg` (or `.png`, `.bmp`) - the sky in one piece, twice as
  wide as it is tall. It is turned into the six faces when it is loaded. This is
  what `assets/sky.jpg` is, and it is the sky a scene gets.
- **A folder of six faces**, named after the direction each one looks at -
  `px`, `nx`, `py`, `ny`, `pz`, `nz` - with `.png`, `.jpg` or `.bmp`.

Two places are looked at:

- `<project>/` - the project's own sky, which wins.
- `assets/` - the sky the engine ships.

The six faces are six ordinary textures and not one cube map, and the cube is
drawn with the texture coordinates written out by hand. A cube map is looked up
through the fixed function texture coordinate generation, and not every driver
still has that: where it is missing, the sky comes out as a sphere map - the
picture wrapped around the camera - and it turns with the player when the
camera turns. The quads look the same everywhere.

### The sky the engine ships

`assets/sky.jpg` is a 4096x2048 picture made by `tools/skygen.cpp`: a
generator that draws a day - a sun, a layer of clouds, a horizon and a dark
ground - and writes it out. It is a tool rather than part of the engine because
the picture is made once; the file it writes is the asset, and the engine loads
it like any other texture.

```
cmake -S . -B build -DSPECTRA_BUILD_TOOLS=ON
cmake --build build
build\skygen.exe assets\sky.jpg
```

Nothing has to be changed to have a sky: `assets/sky.jpg` is the sky a scene
gets. Copy it into a project as `sky.jpg` to give that project a sky of its
own, and replace it with a picture of your own to change it.

## Sound

The engine has sound: a device, the sounds loaded into it, and the playing of
them. It is the `audio` layer, and miniaudio is what talks to the sound card.

**The sounds are files, and the name of a sound is the name of its file without
the extension.** A `.wav` (or `.ogg`, `.mp3`, `.flac`) dropped into
`assets/sounds/` is a sound the scripts can play and a body can carry, with
nothing else to be written. The engine loads the whole folder at startup and
says how many it found in the log.

Three things can be done with a sound:

- **Play it** - from a script, with `engine::playSound("hit")`, or from the
  **Sounds** panel (View menu), which lists the loaded sounds and has a button
  that plays each one.
- **Give it to a body** - the `+` button in the Sounds panel gives the sound to
  the body that is selected. The body then makes that sound when it hits
  something, louder the harder it is hit. The inspector shows the sound a body
  carries, with a button that plays it and one that takes it away.
- **Put it in a script** - `engine::playSound`, `engine::playSoundLoop`,
  `engine::stopSound`, `engine::setMasterVolume` and the rest.

A machine with no sound card is a machine where everything else still has to
work: the device fails to start, the log says so, and the game runs in silence.

### The sounds the engine ships

`assets/sounds/` holds three short pieces, two seconds each, made by
`tools/soundgen.cpp` - a generator that writes 16 bit PCM WAV by hand. They are
made to be told apart by ear: a steady note with a tremolo, a chirp from low to
high, and a knock.

```
cmake -S . -B build -DSPECTRA_BUILD_TOOLS=ON
cmake --build build
build\soundgen.exe assets\sounds
```

The three files are test sounds, not music: they are there so that a machine
with a sound card has something to make, and so that the engine's own sound can
be checked by ear.

To make a skybox of your own from six images, drop them into a folder named
after the direction each one looks at and point a project's `sky/` at it.

## Third-party licenses

Spectra3D itself is Apache-2.0 (see [LICENSE](LICENSE)). Every vendored component
below is under a permissive, GPL-compatible license, so nothing here pulls the
project away from Apache-2.0. The full text of each license ships with the component
under `third_party/`.

`third_party/` is grouped by what a component is **for**, not by one folder per
project: `models_lib/` holds everything that turns a model file into vertices,
`stb/` holds the public-domain single-header libraries, and `json/` the vendored
header-only JSON parser.

| Component | Version | License | Text |
|---|---|---|---|
| GLFW | 3.5.1 | zlib | `third_party/glfw/LICENSE.md` |
| cgltf | - | MIT | `third_party/models_lib/cgltf/LICENSE` |
| rapidobj | - | MIT | `third_party/models_lib/rapidobj/LICENSE` |
| miniply | - | MIT | `third_party/models_lib/miniply/LICENSE.md` |
| ufbx | - | MIT | `third_party/models_lib/ufbx/LICENSE` |
| FreeType | 2.14.3 | FTL | `FTL.txt` (full text), `third_party/freetype/LICENSE.TXT` |
| AngelScript | 2.38.0 | zlib | inside `third_party/angelscript/SDK-2.38.0.zip` |
| nlohmann/json | 3.12.0 | MIT | `third_party/json/json.hpp` |
| miniaudio | 0.11.25 | public domain **or** MIT-0 | `third_party/miniaudio/miniaudio.h` |
| stb_image | - | public domain **or** MIT | `third_party/stb/LICENSE` (one file for the whole family) |
| stb_vorbis | 1.22 | public domain **or** MIT | `third_party/stb/LICENSE` (same) |

Every stb library is released under the identical public-domain **or** MIT-0 terms,
so `third_party/stb/LICENSE` is the single text covering `stb/` as a whole - it is
not a copy that could drift from the individual libraries, and no `stb_image.h` or
`stb_vorbis.c` carries a second copy of it.

miniaudio embeds dr_wav, dr_flac and dr_mp3 itself but **not** stb_vorbis: its
Vorbis backend is behind `STB_VORBIS_INCLUDE_STB_VORBIS_H` in the header, so `.ogg`
needs this decoder compiled alongside it. Neither one is in the build yet: the
engine has no audio at all, and the decoder here is for when it does.

FreeType is dual-licensed upstream and this project uses the **FreeType License
(FTL)**, the permissive option. The FTL requires crediting the FreeType Project in
the product documentation, so:

```
Portions of this software are copyright © 2026 The FreeType Project (https://freetype.org). All rights reserved.
```
