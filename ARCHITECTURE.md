# Spectra3D Architecture

Spectra3D is a small 3D engine with a built-in editor. This document is the map:
what the layers are, which way dependencies point, and the handful of rules that
exist because breaking them produced a specific, reproducible bug. Where a rule
looks arbitrary, the reason is given; that is the point of the section.

- [The layer graph](#the-layer-graph)
- [Rules that hold the structure together](#rules-that-hold-the-structure-together)
- [The layers](#the-layers)
- [The UI frame contract](#the-ui-frame-contract)
- [Docks, zones and the saved workspace](#docks-zones-and-the-saved-workspace)
- [The launcher](#the-launcher)
- [The application state machine](#the-application-state-machine)
- [When a frame is drawn](#when-a-frame-is-drawn)
- [On-disk formats](#on-disk-formats)
- [The scripting layer](#the-scripting-layer)
- [Defects this rewrite fixed](#defects-this-rewrite-fixed)
- [How it is verified](#how-it-is-verified)

---

## The layer graph

Dependencies point **upward only**. A layer never includes a header from a layer
below it.

```
           player       Player: the program that plays a game
              |
              app        Application, GameHost, Launcher, FixedClock, main
                |
              editor      Editor, panels/, Picking, Gizmo, AssetBrowser
                |
              script      ScriptHost, BindEngine/BindScene/BindCamera/BindString/BindInput
                |
              scene       BodyFactory, SceneSerializer
                |
                ui         Theme (palettes), UiContext
                |
               gui        GuiCanvas, GuiRenderer: the interface of a game
                |
              render      Camera, Color, SceneRenderer, UiRenderer,
                |          TextAtlas, PrimitiveCache, GlInfo
              assets      MeshAsset, MeshRegistry, MeshImport, MeshUtil, loaders/
                |
              audio       SoundDevice: the device, the sounds, the playing
                |
              physics     RigidBody, Material, PhysicsWorld
                |
             +------+------+
             |             |
         platform        i18n       Window, Filesystem      Localizer
             |                         (core + platform)
             +------+------+
                    |
                   core     Math, Log, CrashLog, Str, Project

   third_party/     glfw, freetype, angelscript, miniaudio,
                models_lib/ (cgltf, rapidobj, miniply, ufbx),
                stb/ (stb_image, stb_vorbis), json/
```

`ui` sits above `render` because `UiContext.hpp` takes `Rect` and `Color` from
there, while `Theme.hpp` is only colours. `gui` sits above `render` and below
`ui`: the canvas is made of rectangles, colours and letters, and the renderer
that draws it needs the same three the renderer of the editor's own interface
uses. `script` sits above `scene` —
`BindEngine.cpp` creates bodies through `BodyFactory` — and below `editor`, because
`Application` runs it alongside the `MeshRegistry` and the camera. `GlInfo.hpp`
lives in `render` because it is about the context, and it reads `kGlVersionMajor`
from `platform/Window.hpp`: a dependency upward, which is exactly where it belongs.
`audio` sits between `assets` and `physics` because it is a service both the
scene and the shell use and neither of them owns: a body carries the *name* of a
sound and the device decides what that name means.

## Rules that hold the structure together

1. **One direction.** `physics` does not know about `assets`; `render` does not know
   about `editor`; `editor` does not know about `app`. A violation is visible
   immediately: the function you are writing belongs a layer up.

2. **No globals, except the log.** Everything lives in an instance somebody owns.
   The single exception is `s3d::Log`, which every layer reads — the model loaders
   included — and threading an instance of it through every call stack buys nothing.
   `s3d::CrashLog` is the second one, for the same reason and a harder one: the
   handler that runs when the program dies cannot be handed an instance of anything.
   *The scripting layer is the one exception to the exception:* AngelScript's global
   functions have no receiver, so `ScriptHost.cpp` keeps `g_state`/`g_sink` and
   publishes the single active `ScriptState` through `activeState()`. There is only
   ever one: a second `Host::initialize()` refuses.

3. **Everything is in `namespace s3d`.** No namespace-level globals by convention,
   and the ones that do exist are `inline constexpr` in a header so they have one
   address and one spelling.

4. **Headers in `include/`, implementations in `src/`.** Both directories use the
   same layer split, so `include/core/Log.hpp` sits beside `src/core/Log.cpp`. The
   **root of `include/`** is what goes on the include path, which is why every file
   writes `#include "core/Log.hpp"` — the same spelling from a `.cpp`, from another
   layer and from `tools/`, with no `../` that breaks when a file moves. The
   vendored `include/json/json.hpp` (header-only nlohmann) lives there too, behind
   the same rule.

5. **Third-party libraries in `third_party/`, data in `assets/`.** The repository
   root holds only `CMakeLists.txt`, the two build wrappers, the docs, the licence
   and the directories. `build/` is created by CMake and is in `.gitignore`.

6. **`CMakeLists.txt` is the single source of truth for the build.** The `.bat` and
   `.sh` files only call CMake. The engine's file list is not duplicated anywhere
   else; the single `file(GLOB)` is AngelScript's ~100 sources, with
   `CONFIGURE_DEPENDS` so a new file is picked up on the next build. Options:
   `SPECTRA_SCRIPTING` (ON), `SPECTRA_BUILD_TOOLS` (OFF), `SPECTRA_WERROR` (OFF).
   The project version and the icon directory are *read out of
   `include/core/Project.hpp`* rather than written a second time.

7. **The targets, and the boundary is the layer graph.** The engine's own sources are
   compiled once into the object library `spectra_engine`, and that is used twice: as
   the static library `spectra_runtime`, which a program links, and as the shared
   library `SpectraPlayer.dll` - the core as a file, which a project carries in its
   `lib/` folder and a built game has beside it. So a program is one file that
   cannot lose its core, cannot fail to find it and cannot be of the wrong version,
   and a project still says which core it was made with. One compilation, two shapes
   of the same code, so the two cannot drift apart.

   On top of the core there are three programs' worth of code, and two programs:

   - `spectra_game` (static) is the **game**: the world, the picture of it, the sound
     and the scripts. It is `app/GameHost`, and it knows nothing of the editor - no
     panels, no gizmos, no project list.
   - `spectra3d` (executable) is the **editor**: `Application`, `Launcher` and the
     editor itself, over that game. It plays the game through it.
   - `spectra_player` (executable, `SpectraPlayer.exe`) is the **player**: the game
     and nothing else - a window, a loop, and the boot file beside it that says
     which game to play. There is no launcher in it and no editor in it, and it is
     what a build of a game is made of: the editor copies this program into the
     build folder and renames it after the game.

   The split is not cosmetic - it is what stops the editor and the player from
   drifting apart. Both are built from the same `GameHost`, so the game a player
   plays is the game the editor edits. Nothing below `app/` includes either of them
   (the one exception is `Application.hpp`, which owns both).

   The shared library exports everything the program uses
   (`WINDOWS_EXPORT_ALL_SYMBOLS`, and `--export-all-symbols` for the GNU linker), so
   the layer headers carry no decoration of their own.

   A shared library cannot sit in a folder of its own on Windows: the loader looks
   for it beside the program, in the system folders, and on `PATH`. The built game
   therefore keeps the program and the core together.

   `tools/scripttest` used to name eighteen engine sources by hand. It now links
   `spectra_runtime`, because a source list is not a dependency list: the old one
   could not see a file added to the engine until something stopped linking.

---

## The layers

### `core`

`Math.hpp` (`Vec3`/`Quat`/`Mat3`, `clampf`, `lerpf`), `Log` (stderr + `engine.log` +
the ring buffer the editor console reads), `CrashLog` (the handlers and the report of
a crash), `Str` (UTF-8, paths, extensions) and
`Project` (`Settings`, `ProjectLibrary`, `ProjectInfo`, the project file-name
constants). Nothing above the standard library.

`Project.cpp` is the one place `core` allows itself `#include
"platform/Filesystem.hpp"`: enumerating directories and finding the executable are
both needed to scan projects and to read and write files. It is a deliberate,
named exception rather than a habit.

`Settings` is machine-level and lives next to the binary (`settings.ini`), so the
installation is portable. `ProjectInfo` is project-level and lives in the project
directory, so a project folder is a complete unit.

### The crash report

**A CRASH IS THE ONE FAILURE THE ENGINE CANNOT LOG ITS WAY OUT OF.** The log lives
in a buffer, and the buffer dies with the process: the last thing the program said
is exactly what is lost when it falls over. So the report is written by the handler
itself, straight to the disk, and it **carries the last words of the log with it** —
`Log::copyEntries()` takes a copy of the ring buffer while the handler runs, and
that copy is the only account of what the program was doing.

The handlers are three, because there are three ways to fall over:

- **The machine's own**, for a fault nobody catches: a memory that was touched and
  was not there, a stack that ran out. On Windows that is a vectored exception
  handler (`AddVectoredExceptionHandler`), which is called before anything else and
  is the only place that sees the address of the fault; on POSIX it is `sigaction`
  for `SIGSEGV`, `SIGABRT`, `SIGFPE`, `SIGILL` and `SIGBUS`. The program still dies
  the way it was going to: the handler writes the report and lets the fault go on.
- **The C runtime's**, for a program that stopped itself: `std::abort` and the
  signals the runtime turns a fault into.
- **The terminate handler's**, for an exception that got out of the program.

An exception that reaches `main()` is caught there instead — `main()` writes the
report with the message the exception carries. The terminate handler does **not**
rethrow the exception to read it: a rethrow inside a terminate handler starts a
second unwinding, and on the GNU runtime that is enough to break the unwinding
itself.

The report is a **file next to the program**, named `crash-001.log`, `crash-002.log`
and so on — the first free number is taken, so the reports of several crashes are
all kept and they are in the order they happened. It holds the reason, the address
of the fault, the stack, and the tail of the log.

**THE REPORT IS WRITTEN WITHOUT THE HEAP.** A crash handler runs with the heap in
an unknown state, so the report formats every line into a stack buffer and writes
it with one call — no `std::string`, no `fprintf`, nothing that can ask for
memory. The path of the file and the name of the program in it are plain buffers
for the same reason.

Two things about it are worth knowing before changing any of it:

- **The unhandled-exception filter is not installed on Windows.** The vectored
  handler sees the same faults, and a filter of its own changes the way the
  machine walks the frames — on the GNU runtime that is enough to break the
  unwinding of a C++ exception, which is the one thing the engine does not want
  broken.
- **The stack is read from the thread that died**, so the frames of the handler
  are at the top of it and the frames of the code that broke are under them. The
  names of the functions come from DbgHelp on Windows and from `dladdr` elsewhere,
  and both are optional: without them the report still has the addresses.

### `platform`

`Window` wraps GLFW and owns the input state as well: edge- and level-triggered
mouse and keyboard flags, a text buffer, the clock, and an `InputEvents` queue holding
the frame's edges in arrival order. The instance is reached
from the GLFW callbacks through `glfwSetWindowUserPointer`, so there is no
file-scope "current window".

`InputEvents.hpp` is header-only and free of GLFW — a `std::vector` of
`{type, code, value}` with a per-frame cap. It sits next to the bool arrays rather than
replacing them, because the two answer different questions; see
[The frame loop](#the-frame-loop) for what only the queue can see.

**The window icon** is set here too. `Window::applyIcon(name)` loads
`<exeDir>/assets/image/icons/<name>` — the name comes from `settings.ini`, so the
icon is a setting rather than a constant — and hands it to the shell through
`WM_SETICON` at both sizes the title bar and the task bar ask for. The path is
built **wide** (`GetModuleFileNameW` + `LoadImageW`) rather than through
`fs::executableDir()`: `LoadImageW` has the same prototype on every compiler, and a
narrow path would fail on a folder whose name is not ASCII. A missing or unreadable
file is not an error — `makeMarkIcon()` draws a small prism instead, so losing the
file costs the icon and nothing else. The same file is embedded into the `.exe` by
CMake as a resource when a resource compiler exists (MinGW: `windres`, MSVC:
`rc.exe`); on Linux there is none, which is why it is optional.

`Filesystem` is the only place with `#ifdef _WIN32` for filesystem work: directory
listing, the executable path, finding the asset root, `adoptAssetWorkingDir()`, and
the shell folder dialog. There is no separate GL loader — the engine targets the
fixed-function pipeline and includes `<GL/gl.h>` directly.

**THE PATHS ARE IN UTF-8, AND THE WINDOWS CALLS ARE THE WIDE ONES.** The engine keeps
its paths in UTF-8, because that is what the files it writes say (`project.Spectra`,
the scene, the translations), and Windows has two spellings of every call that takes
a path: the narrow one, which reads the bytes in the machine's own code page, and the
wide one, which reads them as they are. A project in a folder with a Cyrillic name is
a path the narrow calls cannot find: the bytes are read as something else, and the
folder is not there.

So `Filesystem` uses the wide calls - `GetFileAttributesW`, `FindFirstFileW`,
`CopyFileW`, `_wfopen` - and the names the listing gives back are converted to UTF-8
as they come in. The libraries that can only be given a narrow path - the model
readers, the sound device, the font library, the picture writer - are given the path
through `fs::systemPath()`, which converts it to the machine's code page at the last
moment. A name the machine's code page cannot spell is a name those libraries cannot
open; everything the engine does itself goes through the wide calls and does not
care.

**THE PICTURE OF THE SCREEN IS A FILE.** `Screenshot.hpp` writes what is on
the screen into a PNG, and **F12** takes one — in the editor and in the
launcher alike. The file goes next to the program, numbered so that a picture
taken yesterday is not overwritten by one taken today.

Half of the bugs this engine has ever had were found by looking at a picture
of it, and a picture that has to be taken with a screenshot program and put
in a folder by hand is a picture that is not taken. The read is out of the
framebuffer, so it has to happen **before the buffer is swapped**: after the
swap the picture on the screen is the one that was there before.

**The asset root is found in the places a program is run from.** Next to the
binary, then `../share/spectra3d` (where `make install` puts it), then the source
tree the build knows about, then the working directory. A program that cannot
find its assets has no font, no sky, no sounds and no language, and the engine
that draws no letters is an engine that cannot be used — so the places are
listed rather than guessed at. The executable path is asked of the OS the way
each OS knows it: the module path on Windows, `/proc/self/exe` on Linux, and
`_NSGetExecutablePath` on macOS, which has no `/proc` at all.

### `physics`

`RigidBody` (mass, inertia, bounds, warm-start cache, `Material`) and
`PhysicsWorld` (gravity, contact generation, solver, sleeping). It knows nothing
about models: a body stores a mesh *index*, which the renderer resolves.

### `assets`

`MeshAsset`, `MeshRegistry` (the owner of every imported model — there is no global
registry any more), `MeshUtil` (shared bounds/normals/size fitting) and one file per
format in `loaders/`.

`LandformSettings` is here because a mesh the engine made carries the settings it
was made of: `MeshAsset::landform` is the recipe of a ground, and the recipe is
what a scene writes down so that the ground can be made again.

`CloudSettings` is here for the same reason: `MeshAsset::cloud` is the recipe of
a sky, and the recipe is what a scene writes down so that the cloud can be made
again. The mode — flat or a volume — is in the recipe too, because the two are
different clouds and not two settings of one cloud.

### `audio`

`SoundDevice`: the audio device, the sounds loaded into it, and the playing of
them. It is an instance and not a global — the engine makes no sound until
somebody starts it, and a machine with no sound card is a machine where
everything else still has to work, so a failure to start is a line in the log and
not a reason to stop.

Three things about it are worth knowing before changing it:

- **The sounds are named, not paths.** A script says
  `engine::playSound("hit")` and the registry knows which file that is; the
  same name is what a body carries in the scene, so a sound can be given to a
  body by a script as well as by the inspector. The `physics` layer carries the
  name and knows nothing about files — the same arrangement the mesh index has.
- **Each sound has three voices.** Two bodies hitting the floor in the same
  frame are two sounds, not one sound played twice, and a sound that is already
  playing is played again from the beginning: the same sound twice is the same
  sound twice, not a half of one and a half of another.
- **The device is behind a pointer.** miniaudio's types are anonymous typedefs
  and cannot be forward declared, and a header that included four megabytes of
  miniaudio would be included by everything that so much as mentions a sound.
  `SoundDevice.hpp` names an `Impl` and only `SoundDevice.cpp` ever sees what is
  behind it.

miniaudio is one header with the implementation in it, so exactly one
translation unit defines `MA_IMPLEMENTATION` — the one that owns the device.

### `render`

`Camera`, `Color`, `PrimitiveCache` (display lists), `TextAtlas` (FreeType),
`UiRenderer` (the batched 2D renderer with `pushClip`/`popClip` over the scissor),
`SceneRenderer` (the 3D scene, cutouts, the gizmo). 3D and 2D are separate: the
editor never draws through `SceneRenderer` and the scene never draws through
`UiRenderer`.

`Skybox` is the sky the world is seen against: six faces drawn on a cube around
the camera. **The six faces are six ordinary textures, not one cube map**, and
that is not a style choice. A cube map is looked up through the fixed function
texture coordinate generation, and that is not a thing every driver still has:
on a machine without it, `glTexGen*` answers `GL_INVALID_ENUM` for every
target and every parameter, the cube map falls back to the mode it was born
with - the sphere map - and the sky comes out as a picture wrapped around the
camera. It is drawn correctly and it looks fine, and it turns with the player
when the camera turns, which is the one thing a sky must not do. Six quads with
the texture coordinates written out by hand are the same picture on every
driver, and the coordinates are the cube map rules spelled out, so a face looks
the same whether it came from a file or from a panorama.

**THE MESHES ARE ON THE CARD, AND THEY STAY THERE.** Every mesh used to be
drawn out of the memory of the program: the driver was given a pointer to the
vertices and copied them into the card again, for every frame, for every body.
A ground of nine thousand vertices is three hundred kilobytes copied sixty
times a second for a picture that did not change, and a cloud of two hundred
puffs is worse.

The buffers are made once — the first time the mesh is drawn — and the card
keeps them. **THE BUFFERS BELONG TO THE INDEX AND NOT TO THE PICTURE**: the
mesh in the registry never changes under an index, because the registry never
replaces an entry. The name and the size of the mesh are remembered anyway, so
that a registry which is replaced — and starts its numbers again — says so
instead of drawing one mesh out of the buffers of another.

**THE STATE IS LEFT AS IT WAS FOUND.** The attributes are turned off and the
buffers are let go of after every mesh: the grid and the gizmo are drawn by the
fixed pipeline out of the memory of the program, and a buffer that is still
bound makes the fixed pipeline's own pointers mean something else.

**THE SHADOWS ARE A PASS OF THEIR OWN.** `ShadowMap` is the picture of how far
away the sun hit something first: one texture, one layer per *cascade* - a
cascade is a stretch of the scene with a map of its own, so that the near
ground gets the texels and the far ground does not steal them. The map holds a
**distance in the world** and not a depth of the window, because the sun is a
directional light and its projection is orthographic: the distance in the map
is the distance in the world, and a shadow is one comparison of two numbers
that mean the same thing.

The map is fitted to the **scene and not to the view**. The map is a picture of
the world as the sun sees it, and the sun does not care where the eye is: a
map fitted to the view is a different picture every time the eye moves, and
then it has to be drawn again every time the eye moves - which is every frame
of a flight, and the whole point of the map was to stop that. So the map is
drawn again only when the world or the sun has changed, and the state it
depends on is remembered and compared: where every body that casts is, how
big it is, **how it is turned** (a thing that spins in place stands in the
same place and casts a different shadow), and where the sun is. The sun is a
setting, and a setting that moves turns the map into a different picture.

**THE STRETCH IS CHOSEN PER FRAGMENT.** A cascade is a sphere around the
middle of the scene, and the cascade a fragment belongs to is the smallest one
that reaches it - a question about the distance to the middle, and the distance
to a point is not a straight line and cannot be interpolated across a
triangle. A ground is two triangles forty metres across and the distance to
the middle is the same at all four of its corners, so a vertex stage that
chose the cascade would put the whole ground in the outermost one, and the
shadow of a box in the middle of the scene would fall on a cascade the box
was never drawn into. It is chosen in the fragment stage, where the distance
is the distance.

**THE BIAS IS IN METRES.** The bias is how far the surface is pushed towards
the light before it is compared with the map, and it is a distance in the
world and not a number in the map's own units: the map holds a distance as a
fraction of how deep the box of its cascade is, and that depth is different in
every cascade. Half of the bias pushes the surface along its own normal, which
keeps the shadow touching the thing that casts it, and the other half pushes
it along the light and grows with the slope of the surface, which is what the
stripes a surface draws on itself are.

**A MODEL IS NOT PUSHED THROUGH THE MAP FOUR TIMES.** The near cascades are
boxes in the middle of the scene, and a model that stands at the edge of it
has no shadow in them at all, so a body is only drawn into the cascades it
reaches. The map is also not kept at all when the shadows are off: a 4096
map with four cascades is a hundred and thirty megabytes of picture, and the
machine that cannot afford it is exactly the machine that wanted the shadows
off.

**THE GROUND OF THE EDITOR IS A SHEET, NOT A GRID.** The grid is a window on
the world grid: a hundred and twenty minor lines a unit apart, fading out
between twenty-eight and sixty metres. A shadow on a line is a shadow nobody
can see, so the model would stand on nothing and look like it is flying.
There is a plain sheet under the grid that is **not drawn** and only takes
the shadow - what the eye sees is the shadow on it, and the grid over it. The
sheet is drawn with the same program as the bodies, in a mode of its own
(`uSurfaceMode`), and it is six vertices rather than four: a quad of four
vertices is two triangles as well, and the seam between them is a place
where the two halves can disagree.

The sky is drawn first, with the depth test off and the depth write off, so it
is behind everything and hides nothing. The cube is put where the camera is and
scaled up, and it is **not turned**: the model matrix is a translation and a
scale and nothing else, so the cube's own frame is the world's frame, and the
direction a texel is looked up in is a world direction.

**THE SKY IS DRAWN WITH THE SCENE'S PROGRAM, SO THE FOG REACHES IT.** The sky
is the far distance, and the air between the eye and the sky is the same air as
the air between the eye and everything else - so a sky drawn without the fog
stands in front of the fog like a wall, and the horizon is a line. The sky is
`uSurfaceMode` 2 of the scene's program: the picture of the sky with the fog
over it, and no light and no shadow.

**THE FOG IS AN INTEGRAL, NOT A DISTANCE.** The fog is thickest at the bottom
of the world and thins as it goes up, so the amount of it between the eye and a
thing is the integral of a density along the line between them - and an
integral can be done exactly, which is the reason it is worth writing out.
The integral is done in **two pieces**, because the fog has a floor: below the
floor the density is the density of the floor, and above it the density falls
away with the height. A ray that looks down into a hollow gets the thick air
of the floor and not an exponential that runs away.

**THE FOG CARRIES THE SUN.** The colour of the fog is the colour of the air
plus the light of the sun scattered forward by the drops the fog is made of -
the Henyey-Greenstein phase function, in **two lobes**: the forward one is the
aureole around the sun and the backward one is the light that comes back from
behind the drops. They are mixed so that the fog is as bright on average as
it would be with no scattering in it at all - the glow around the sun is paid
for out of the air around it and not out of the light of the sun.

**THE FOG IS ONE FILE.** `shaders/fog/fog.glsl`, and the world, the particles
and the water all have it. The loader reads a line of `#include` and puts the
file where the line was, so that the air in front of the world and the air in
front of the smoke and the air in front of the water are one air and not
three.

The fog belongs to the renderer (`Fog.hpp`) and not to the shadows: it has no
map, no cascade and no bias, and it is not a comparison of two numbers. It is
the air. The settings are the project's, like the shadows are, and they are
set where the fog can be seen - the View menu, next to the shadows.

**THE WATER IS A PROGRAM OF ITS OWN.** `water/water.vert` and
`water/water.frag`, and not a mode of the scene's program: the water is
transparent and the world is not, and a picture that is half of one and half of
the other is a picture nobody can look at.

**THE WATER IS A SHEET AND NOT A CUBE.** A cube has a lid and a floor and
four walls, and the walls of a lake are not what a lake is. The sheet is a
grid that runs from -1 to 1 in both directions and is scaled to the body
that is the water, so that two sheets of water in the same place have the
same waves in them.

**THE COLOUR OF THE WATER IS A DEPTH.** The water is bright where it is thin
and dark where it is deep, and the depth of the water at a place is the depth
the body was given, less what the shore takes off it and more what a wave
has raised. What comes back of the bottom is the light of the bottom with
the water's own colour taken out of it, and the red is the first to go.

**THE RIPPLES ARE MADE PER FRAGMENT.** The mesh carries the waves, and the
mesh is a grid: anything smaller than a cell of it is made in the fragment
stage, or the water is a sheet of glass with four big waves in it. They fade
out with the distance, because a ripple that is smaller than a pixel is a
shimmer and not a ripple.

**THE BOTTOM IS BENT, AND THE BOTTOM IS A COPY.** The picture of the scene
is copied into a texture before the water is drawn over it, and the water
reads the copy: the same pixels the water is drawn over, so that what shows
through the water is the world that is really there. The bend is the
difference between the way the light would have come and the way it does
come, in the eye's own frame.

**THE WATER IS A PROPERTY OF THE BODY.** `physics/WaterSettings.hpp`, and
the settings live with the body and not in the renderer: two sheets of water
in one scene can look nothing alike, and the water is a property of the body
that is water. They are set in the inspector, where the water can be seen.

**THE WAVES ARE FOUR FIXED GERSTNER WAVES.** A Gerstner wave has a sharp
crest and a flat trough, because the water at the top of the wave is thrown
forward - and the cost of that is that the normal is not the slope of the
height any more, so the normal is worked out of the two directions the
surface runs in. The waves are in the world and not in the mesh, so two sheets
of water in the same place have the same waves in them, and the sheet has to
be cut, because the waves are worked out per point.

**THE WATER IS BLENDED WITH A PREMULTIPLIED ALPHA.** The colour is written
ready to be added to the picture and the bottom comes through what is left of
the transparency. It is the only way the reflection survives: the sky in the
water is light that comes *off* the surface, and a mixed alpha multiplies it
by the transparency as well - so a shallow river seen along its length would
lose the mirror that is the whole point of looking at it along its length.

**THE SHADOW ON THE WATER IS ONE TAP, AND IT IS OFTEN NOT ASKED FOR.** The
water takes the sun's map and the stretches of the scene, and reads the map
once. Sixteen reads over half the screen is a price for an edge that the
water's own waves make noisy anyway, and what the shadow takes away of the
water is the shine of the sun. The read is skipped altogether where the sun's
light cannot reach the water at all - a surface that faces away from the sun
is dark whether the sun is behind a mountain or not. And the water does not
cast a shadow: a sheet of water in the map is a sheet of water that shadows
itself.

**THE SHADOW IS NOT ASKED FOR WHERE THE SUN CANNOT REACH.** The same rule in
the world's shader, and it is the most expensive thing in the frame: with the
soft shadows a fragment asks the map sixteen times, and the back of every
object in the picture asks it for nothing it will use. A surface that faces
away from the sun has no direct light and no shine, and it is dark whether
the sun is behind a mountain or not.

**The grid follows the camera.** The grid is a *window on the world
grid*: the patch is snapped to the major step and moved with the camera, so
every line lands back on the world position it was on before and there is no
edge of the grid to walk to. It is drawn in two parts, because one part cannot
be both fine and endless - the minor lines are one unit apart and are only
worth drawing near the camera, and the major lines are ten units apart and
carry the grid out to the horizon, where one unit is smaller than a pixel and
would only alias.

**There is no arena.** The world has no box around it: the physics has no
walls, and nothing is drawn that says where a thing would stop. A game
decides where its own bounds are, and a hard-coded box that stops a thing in
mid air is not a bound - it is a bug with a shape. What falls out of the world
keeps falling.

**Every line is cut into short segments**, so that each segment can have its
own transparency. A line drawn as one long segment can only be faded as a
whole, and both of its ends are far away - so a line faded as a whole is
either transparent everywhere or opaque to the end of the patch. The
transparency of a vertex is its distance to the camera, so a line dissolves
along its length and the grid ends at the horizon rather than at a boundary.

`TextAtlas` is a fixed table of 240 glyphs: ASCII, Cyrillic `U+0410..U+044F`, eight
extra Cyrillic letters, and **Latin-1** (`U+00C0..U+00FF` plus `« » œ Œ ¡ ¿`).
Latin-1 arrived with German, French, Italian and Spanish: without it `ä ö ü ß`,
`é è ê ë à ç ù`, `á í ó ñ ü` and `¿` drew as empty boxes while the language files
carried two hundred keys that went nowhere. Everything fits: one 1024×1024 page,
18-pixel rows.

**The font is shipped, not borrowed.** It is JetBrains Mono, in
`assets/fonts/`, with its licence beside it, and the atlas is baked from it
first. The system fonts are the fallback and nothing more: a system font is a
different font on every system - Consolas on one, DejaVu on another, nothing at
all on a third - and the engine that draws no letters is an engine that cannot
be used. One font in the assets is the same letters on every machine the engine
runs on, and it is the same letters for the interface and for the code.

**THE ATLAS AND THE LETTERS ARE THE SAME TABLE.** The atlas is baked with
`codepointAt(slot)` and read with `glyphIndex(codePoint)`, and the two have to
agree about which slot holds which letter. They did not: every Cyrillic
letter was drawn as the one after it, and a word in Ukrainian came out as a
word in a language nobody speaks - the letters were right and the words were
not. `editortest` asks the two tables the same question about every slot, and
the answer has to be the same number every time.

**THE PARTICLES LIVE HERE, AND NOT IN THE SCENE.** `ParticleSystem` is the
pool of the particles, the emitters they come from, the step and the drawing.
It is in the `render` layer because it is a thing that is *drawn*: it owns a
program and two buffers, and the renderer is the class that owns the GL
objects. The simulation is a few lines of arithmetic; the drawing is the part
that needs a place, and a place in the layer graph is a real constraint.

Three things about it are worth knowing before changing it:

- **The pool is fixed, and it is a ring.** Four thousand particles, and a new
  one takes the place of the oldest. A pool that grows is a pool that
  allocates in the middle of a frame, and a frame that allocates is a frame
  that stutters. A pool that is too small is an effect that loses its oldest
  particles, which is what an effect does when there are too many of them.

- **An emitter is scene data, and it is in the scene file.** The place, the
  direction, the recipe and the body it rides on - all of it is written into
  `scene.json` and read back out of it, because an effect that is in the
  picture and not in the file is an effect that is gone when the scene is
  opened again. The emitters travel the same way the ground's settings do:
  as the numbers that made them.

- **The same seed is the same effect.** The random numbers are the engine's
  own - a xorshift, because a sine is a different number on two machines - and
  the seed of the effect, the emitter and the burst are mixed into it. An
  effect that looks different on two machines is an effect that cannot be
  tested.

The particles are drawn after the bodies and before the marks of the editor:
a particle is behind a box when it is behind the box. They are drawn with the
depth test on and the depth write **off** - a puff of smoke that hides the
puff of smoke behind it is a puff of smoke in front of it, and the order the
particles are drawn in is not the order they are in.

**THE TWO KINDS OF PARTICLE ARE TWO PASSES.** One is drawn *over* what is
behind it and one is *added* to it, and the difference is one blend function:
smoke hides the thing behind it, and fire makes it brighter. The fog is
different in the two as well, and it has to be - a puff of smoke takes the
colour of the air in front of it, and a spark is light that the air scatters
away, so a spark in a thick mist is a dimmer spark.

The particles are not lit. A fire is its own light, and a shadow map of a
puff of smoke is a shadow of a puff of smoke.

### `ui`

`Theme` (colours and metrics) and `UiContext` — immediate mode. `UiContext` owns the
input snapshot, the hover/active widget ids, the inline numeric editor and the
clip stack for hit testing. Panels describe geometry only; see
[the UI frame contract](#the-ui-frame-contract).

The theme colours are variables rather than constants on purpose: switching a theme
has to be visible to every panel that says `theme::panel`, and there are hundreds of
those references. A function returning a reference would mean the same edit in each
one; a mutable definition means none. `ui` is the only place allowed to write them
(`setActiveTheme`); everything else reads.

### `gui`

**THE INTERFACE OF A GAME, AS NUMBERS AND AS A PICTURE.**

`GuiCanvas` is the data: a canvas is a reference resolution and a list of
elements — a panel, a picture, a line of letters, a button — each with a
rectangle, a colour and a name. `GuiRenderer` is the drawing: the canvas over
the world, in the window's own coordinates, with the letters out of the same
atlas the editor's own interface uses.

The layer is where it is because of two dependencies and no more: the canvas is
made of `Rect` and `Color` from `render`, and `scene` writes it down, so it
sits above the renderer and below the scene.

**THE CANVAS IS PART OF THE SCENE.** The interface of a game is not a thing
the editor draws around the game: it is part of the game, and a game that is
built has to carry it with it. So the canvas is written into the scene file
next to the bodies, read back when the scene is opened, and drawn by the player
program exactly as the editor draws it. `scenes::save` and `scenes::load` take
the canvas; the editor edits **the game's own canvas in place**, so what is on
the screen while editing is what the game shows.

**ONE SCALE FOR BOTH DIRECTIONS.** The canvas is fitted into the rectangle it
is drawn into with a single scale, centred in what is left. A layout made for
1920×1080 is therefore the same layout on a smaller screen, and nothing is
wider than it is tall — a stretched canvas has letters that are wider than they
are high.

**THE ORDER OF THE CANVAS IS THE ORDER OF THE LIST.** The renderer keeps three
batches — flat colours, pictures, letters — and flushes one when the next
thing would have to be drawn over it. A panel under a label reads as a panel
under a label, and a panel that is drawn after a label covers it, because that
is what the list says.

**A CLICK IS A PRESS AND A RELEASE ON THE SAME BUTTON.** The renderer
remembers the button the press started on: a press on one button and a release
on another is not a click on either of them. The mark is written on the element
itself (`hovered`, `pressed`, `clicked`) and is never written to a scene file:
it is what happened, not what was made.

**THE EDITOR EDITS IT, THE GAME ANSWERS IT.** The same canvas is drawn by both,
and the only difference between them is whether the buttons answer the pointer:
in the editor a click picks an element, and in a game a click presses a button.
The editor's own undo stack has a second half for it — `GuiUndoStep` — and
Ctrl+Z takes back whichever of the two was changed last.

### `scene`

`BodyFactory` (every way a body gets created) and `SceneSerializer` (the text scene
format, with models referenced by name).

`Landform` is here as well: the shape of the ground and the mesh cut from it. The
settings it is made of are in `assets/LandformSettings.hpp`, because a ground is a
mesh and the mesh carries them — a scene that is saved says how the ground was
made, and the ground is made again when the scene is opened.

`Cloud` is here for the same reason, and it is the same arrangement: a cloud is
made of noise, the noise decides where the puffs are, and the whole cloud is one
mesh and one draw call. **THE PUFFS ARE INDEPENDENT OF ONE ANOTHER**, so the
making is spread over the threads and each thread writes into a chunk of its own
— the puffs come out in the same order whatever order they were made in, and the
same seed is the same cloud on a machine with one thread as on a machine with
six.

### `i18n`

`Localizer` is an instance, not a global table; every panel holds it by reference.
Bundles are loaded from `assets/lang/*.lang` — 23 of them — and switched from the
launcher. The list of languages lives twice, on purpose: `i18n::Language` for the
runtime and `LanguageTag` for `settings.ini` and `project.spectra`, because `core`
must not depend on the localisation layer. `Launcher::applySettings()` is the one
function that translates between them. The same applies to themes (`ThemeTag` vs
`theme::ThemeId`).

There is **no fallback**: a key a bundle does not carry is rendered as the key. That
is deliberate — a silent English string under a Ukrainian label is a bug report
nobody can file, and a visible `settings.icon` is one everybody can.

### `script`

`ScriptHost` is the AngelScript engine (`asCreateScriptEngine`), module compilation
and execution, and diagnostics. `Bindings.hpp` describes `ScriptState` (a live
`PhysicsWorld`/`MeshRegistry`/`Camera`/`SoundDevice` plus the groups scripts
create) and the registration tables; `BindEngine.cpp` is logging, spawning,
gravity and body access; `BindScene.cpp` is the scene, model import and groups;
`BindCamera.cpp` is the camera; `BindInput.cpp` is the keyboard and mouse, read
from the live `Window`; `BindSound.cpp` is the sound device;
`BindString.cpp` is the engine's own `string` type, because AngelScript has no
standard one. The `engine` namespace is created with
`SetDefaultNamespace("engine")`, because 2.38 does not accept a `namespace` modifier
in `RegisterGlobalFunction`.

Five things to know before changing any of it:

- **One module per script.** `Build()` in AngelScript is atomic: an error in one file
  would zero the functions of the others. The module name comes from the file name
  without its extension (`moduleNameFor`), and `asGM_ALWAYS_CREATE` makes re-running
  a module a replacement of that module alone.
- **Exactly one entry point per script.** `onInit` (or `main`/`start`), otherwise the
  first global `void()`, otherwise (only `shared` functions) nothing. "void()" means
  *no parameters*, so a hook like `void onUpdate(float dt)` is **not** an entry point
  and does not run on load — it only runs from the frame loop. A `void onStop()` is a
  `void()`, so a script that declares nothing else will run `onStop` on load; that is
  the pre-existing rule doing what it always did.
- **Function lookup goes through `resolveFunction()`, not only the engine.**
  `asCScriptEngine::GetGlobalFunctionByDecl` walks only what was registered — the
  `engine::` bindings — and does **not** see functions a script declared itself. So
  `hasFunction()`/`callFunction()` ask the engine table first and then walk each
  module's functions. Without the second half, `callFunction("void onUpdate()")`
  always returned `false`.
  That walk takes the **first** module that declares the name, in module creation
  order. Two scripts declaring the same hook is therefore first-wins, decided by which
  file was loaded first — so `resolveFunction()` emits a warning when it sees more
  than one owner, once per generation. It is a warning and not an error on purpose: a
  duplicate declaration should not stop a project from running.
- **`str::equalsIgnoreAscii` folds only its first argument** (`Str.hpp`): `a` is
  lowercased and `b` has to be a lowercase literal already. The entry-point rule
  therefore spells the names `"oninit"`, `"main"`, `"start"` — writing `onInit`
  would not be found and the rule would silently degrade to "the first `void()`",
  i.e. to a helper.
- **Nothing steps the world here.** The bindings mutate a live `PhysicsWorld` in
  place, so one script runs the same way in edit mode and in Play.
- **The effects are asked for by name.** `engine::emitParticles(x, y, z, 40,
  "fire")` and the engine knows what fire is: the names are the same ones the
  editor uses and the same ones the scene file uses, so an effect can be asked
  for by a person and by a script and mean the same thing. A name the engine
  does not have is said out loud in the log and the effect is not made - a
  script that asks for a fire and gets nothing is a bug in the script, and a
  log line is where such a bug is found.
- **One context, reused.** `Host` keeps a single `asIScriptContext` instead of
  creating one per call, and calls `Unprepare()` on both the success and the exception
  path. A context left mid-execution cannot be prepared again, so skipping this is
  what turns one bad frame into a dead scripting layer for the rest of the session.
- **The context is built lazily.** `runString()` only compiles; the context is created
  on the first call that actually executes something. A script that never runs never
  allocates one.

### `editor`

`Editor` is orchestration only: selection, play mode, input routing, layout. Drawing
is split across `panels/*.cpp`, geometry lives in `Picking`/`Gizmo`, and the model
list lives in `AssetBrowser`. `Editor::handleHotkeys()` holds the shortcuts that
are *not* commands: play, pause, stop, save, load and delete live on buttons and
menu rows, because every one of them mutates or discards the scene and a stray
keypress while typing used to throw work away.

**A field with the keyboard has it.** The shortcut handler returns before it
looks at a key when a text field is focused or a dialog is open, and that is
the rule every shortcut in the editor lives under. It is not a detail: the
number keys pick which way is down, and a digit typed into a search box used
to turn the world on its side — and to save the turned world with the scene.

One file per region of the screen, so they cannot overlap by convention and each one
owns its own arrangement:

| File | Region |
|---|---|
| `panels/TopBar.cpp` | the top bar: menus, tools, transport, counter, search/layers/help |
| `panels/TopBarOverlay.cpp` | everything that drops out of it: menus, search results, Layers, help |
| `panels/LeftColumn.cpp` | the hierarchy with its filter, the stars and the "only favorites" toggle |
| `panels/FavoritesDock.cpp` | favorites: the starred bodies, the same gesture in the same column |
| `panels/ScenePanel.cpp` | the "Scene" strip, the tool column, the navigation balls |
| `panels/InspectorDock.cpp` | the inspector: filter, sections, scrolling |
| `panels/BottomBar.cpp` | the console with its per-level filters |
| `panels/LandformPanel.cpp` | the ground: the shape, the noise, the numbers of it |
| `panels/CloudPanel.cpp` | the sky: flat or a volume, the puffs, the numbers of it |
| `panels/GuiEditor.cpp` | the interface of the game: the tree, the fields, the handles |

**THE TWO MODES OF THE EDITOR.** There are two ways of looking at a project,
and they are modes of the editor and not of the scene: nothing is thrown away
and nothing is saved differently.

- **2D** is the world *measured* and not seen. The camera becomes orthographic
  and looks down the Z axis, so the picture is the XY plane with no vanishing
  point in it; the grid is built in that plane instead of the ground plane;
  and the gizmo has two handles instead of three, because a flat world has two
  directions to move in and one to turn about.
- **The interface** lays the GUI of the game out instead of the world. The
  hierarchy shows the tree of elements, the inspector shows their fields, and
  a click in the viewport picks an element and drags it.

**AN ORTHOGRAPHIC CAMERA IS A DIFFERENT SET OF NUMBERS.** A perspective camera
and an orthographic one answer "where is this pixel" in different ways: the
first divides by the depth, the second does not. So the projection is a
**value** — `picking::ViewProjection` — and not a basis, and every place that
turns a pixel into a ray or a world point into a pixel asks the projection
instead of working the numbers out for itself. The picture and the pointer
cannot disagree about where a thing is, because they are made from the same
numbers.

**THE ORTHOGRAPHIC PICTURE IS THE ZOOM.** `Camera::orthoSize` is the half-height
of the picture in world units, and the wheel changes it. A perspective view
has no zoom here — the wheel is the scroll of the panels — but an
orthographic picture has one, and a flat scene that cannot be made bigger is a
flat scene that can only be looked at from far away.

**A FLAT WORLD HAS NO SUN IN IT.** The 2D view draws no shadow map and no
sheet for it to fall on. The renderer knows which of the two it is from the
camera: an orthographic camera *is* the 2D view, so the grid and the picture
cannot disagree about which plane the world is in.

**Rotating with a gizmo ring.** Rings turn around the body's **world** axes:
`gizmo::axesFor()` rotates them for scale only. Local axes in a rotation meant the
ring travelled with the body, the angle was read in a frame that moves under the
cursor, and the body ended up somewhere else the moment it stopped being axis
aligned.

The angle is taken **on screen**: `Editor::ringScreenAngle()` computes `atan2` around
the body's projected centre, and `applyGizmoDrag()` adds the frame's **shortest
step** (`std::remainder(delta, 2π)`) to an accumulated angle. One measurement per
frame removed three failure modes at once:

- `atan2` has a discontinuity at ±180°: crossing the back of the ring produced a
  difference of a whole turn and the body spun 360°;
- a ray nearly parallel to the ring's plane has no answer, and the answer used to be
  zero — the body snapped to a wrong orientation;
- near the rim, one pixel of movement moved the intersection a long way, so rotation
  either jumped or froze.

The sign of the step depends on which way the camera looks: an axis pointing **at**
the camera turns the other way on screen. A ring seen exactly edge-on projects to a
line, which is true in every editor.

**A tool window is opened and not toggled.** The Landform panel and the Cloud
panel are opened from the Create menu, and the row that opens one does not close
it when it is already open: a row that toggles closes the window the row opened
a moment ago, and the second click is the one that does it.
`Editor::openToolPanel()` opens it, puts it where it covers nothing, and
remembers that it is on screen.

**The File menu and the dialogs.** Ten items, in Unity's order: New Scene, Open
Scene, Save, Save As, New Project, Open Project, Save Project, Build Settings, Build
and Run, Exit. Each does what it says; the ones that throw the scene away ask about
unsaved work first.

A dialog is a `Modal` and `Editor::drawModal()`: one centred panel, a title, fields,
and two buttons at the bottom right.

- **Dialog height is measured, not declared.** The hints are word-wrapped
  (`wrapWords`) and the height is computed from how many lines that produces —
  Ukrainian runs longer than English, and a dialog sized for the shorter one either
  clips the hint or runs into the buttons.
- **The editor cannot reach the launcher and cannot start a process.** Hence
  `ShellRequest { None, NewProject, OpenProject, SaveProject, Build, BuildRun }`:
  the editor asks, `Application` acts. Build and Run has no dialog — it takes the
  folder Build Settings showed.
- **Three things throw the scene away** (New Scene, Open Scene, leaving the project),
  so `requestSceneChange()` asks first. `Pending` and `pendingShell_` remember
  *what* was supposed to happen next: both ways out of a project lead to the
  launcher, but "New Project" and "Open Project" are different commands and only one
  of them is still distinguishable afterwards.

`sceneDirty_` is set by the gizmo, deletion, duplication, the star, the gravity hotkey
and after Save/Open. There is no autosave, so "save?" has to appear before everything
that discards a scene.

### `app`

`Application` is the composition root: it owns the launcher, the editor and the game
they play, runs the frame loop and switches between the two app states. `Launcher` is
the start menu, which lives in `Launcher::Result { Stay, OpenProject, Quit }`.
`FixedClock` is the fixed-step accumulator (60 Hz). `main.cpp` is five lines.

**`GameHost` is the game.** The world, the picture of it, the sound, the scripts and
the loop that runs them - everything a game is, and nothing that draws a dock. The
editor plays the game through it, and the player program is it and nothing else, so
the two cannot drift apart: the same world, the same camera, the same physics, the
same scripts. It is compiled once, into the static library `spectra_game`, and linked
into both programs.

The frame is in three parts, and the shell around the game decides where its own
work goes: `updateCamera()` first (a click is answered with the camera the user was
looking through), then the shell's own input, then `updateWorld()` (the scripts and
the physics) and the picture.

`Application` and `Launcher` sit **in the executable, not in `spectra_runtime`**,
which is rule 7 above: both are the shell around the engine, not the engine. The
consequence is visible in the file list - the runtime stops at `i18n`.

**The step cap and the frame-time clamp are two different brakes.**
`kMaxFrameTime` (`GameHost.cpp`) clamps `dt` to 0.1 s, so at 60 Hz a fully
clamped frame can fund at most **six** 16.7 ms steps. `FixedClock`'s cap is a
second, independent brake, and the header argues for it at length: with the cap
binding first, frame time is `cap * step + render` and stays proportional to the
real cost; without it, a slow step is paid up to six times in a row and the frame
rate pins itself to a plateau it cannot leave. **A cap above six never binds**,
which makes it a second value that has to agree with the first.

### `player`

`Player` is the game as a program: the engine, a window and a loop, with no
launcher and no editor in it. It opens the boot file beside it - the data folder of
a built game is `<name>_Data`, and the manifest in it says which game this is - loads
the world that file names, runs the scripts and plays the game until the window is
closed.

It is what a build of a game is made of: `File > Build` copies this program into the
build folder and renames it after the game, so a build is a program of its own and
not the editor with another name on it. The editor finds it next to itself
(`findPlayerProgram()`), the same way it finds the core.

---

## The project layout

A project is a folder, and the folder is the architecture of the game:

```
<Project>/
  assets/            the content of the game
    textures/        the pictures
    audio/           the sounds
    music/           the long ones
    shaders/         the shaders the project brings
    material/        the materials
    fonts/           the fonts
    models/          the models
    sky/             the sky: a picture, or the six faces of a cube
    lang/            the translations
    icons/           the pictures of the game
    prefabs/         the ready-made pieces
    ui/              the pieces of the interface
    data/            the tables and the settings
    particles/       the effects the project brings of its own
    physics/         the physical materials
  lib/               the core of the engine, as the library it is
  scripts/           the scripts of the game
  scenes/            the scenes of the game
    scene.json
  project.Spectra    the manifest: what the project is
  icon.ico           the picture of the project
```

**The effects of a scene are in the scene.** The emitters - the places the
particles come from, and the recipes they are made of - are written into
`scenes/scene.json` with the bodies and the interface, because an effect that
is in the picture and not in the file is an effect that is gone when the scene
is opened again. The `particles/` folder is for the effects a project brings
of its own: the ones that are not in any one scene.

**The scene lives in `scenes/` and the manifest says which file it is.** The
manifest holds the path relative to the project folder (`scene=scenes/scene.json`),
because a project keeps its scenes in one folder and a project that may have more
than one has to say which one it is. The old name - a bare file name, with the
scene beside the project - is still read: a project made before the folders
existed keeps opening, and it is moved into the layout the first time it is
opened (`ProjectLibrary::migrate()`). The move is the only time the engine writes
into a project it did not create, and it never moves a file over a file that is
already there.

**The project's assets win, and the engine's are the fallback.** `fs::assetRoots()`
is the list of roots to look in: the project's own `assets/` first, the engine's
after it. Every kind of resource asks that list - the models, the textures, the
sounds, the shaders, the sky, the languages, the fonts, the icons - so a project
that brings a file of its own is seen and the engine's is still there for
everything the project did not bring. The engine's own assets keep their older
names (`image/textures`, `sounds`, `image/icons`), so both spellings are looked
for; `core/Project.hpp` holds the list of both (`assetdirs::`).

**`lib/` is the core of the engine as a file.** A project is created with a copy of
`SpectraPlayer` in it, so a project folder says which core it was made with, and
the built game puts the program and the core together - the two of them have to
be together, because that is where Windows looks for a library. The program itself
carries the core as well: the editor and the built game are one file that does
not depend on a library being found.

**The icon of the project is the icon of the window.** `icon.ico` beside the
project is applied to the window when the project is opened
(`Window::applyIconFile()`), and a project without one keeps the mark the engine
draws.

---

This is the part of the editor that is easiest to break, because the batcher and the
input snapshot are both shared, one-shot, and silent when misused.

**One snapshot per frame.** `UiContext::beginInput()` samples the window once, before
the world steps. Every widget then resolves interaction against that same snapshot
while it draws. Nothing re-reads the mouse mid-frame.

**Rectangles before text.** `UiRenderer` keeps two batches and emits every rectangle
before any text. That is why `UiRenderer::flush()` exists: an overlay drawn after a
panel without a flush gets its background *underneath* the labels it covers. The
editor frame is therefore cut into stages, each ending in a flush:

```
top bar          → scene header → viewport tools → docked panels
                 → flush → floating panels
                 → flush → dropdowns and panels hanging off the bar
                 → flush → modal dialog
                 → flush → drag ghost
```

The console is the reason this is written down. It is the bottom of the window, it
is the one panel that is a wall of text, and without the flush after the docked
panels its log lines drew straight through the background of any floating window or
dropdown sitting on top of it.

**State must not change after the frame is drawn.** With on-demand rendering, a
change that reached no frame stays on screen until something else wakes the window.
That is what `overlayOpenedThisFrame_` is for: the press that opens a menu must not
also act on it, or close it, in the same frame.

**A popup owns the pointer.** Every dropdown is drawn *after* the widgets it covers,
so those widgets have already run their hit tests by the time anyone can see that
they are covered. Without a capture, a click on a menu row also pressed the button
underneath it — which is why the caption of a dropdown and a button below it
produced two actions from one click.

The rule lives in the shared layer rather than in each panel, because "do not react"
has to apply to the checkbox, the numeric property, the text field *and* the raw
`if (hovered && ui_.pointerPressed())` that panels write themselves:

- `Editor::handleInput()` calls `ui_.claimPointer(kPopupCapture)` before the panel
  drag and the viewport pick, so neither of them takes a click meant for a menu.
- `pointerPressed()` and `pointerReleased()` return `false` while captured. Every
  widget in the engine goes inert for free; the panels needed no edits.
- `Editor::renderUi()` clears the capture for the top bar (which is what opens the
  popups, and must stay live so a menu can be switched to another one), re-arms it
  for everything below, and releases it again immediately before drawing the popups.
- The popup code reads `rawPointerPressed()` — the same edges with the capture
  ignored — and ignores the press that opened it.

The launcher does the same thing for its two modals, and its dropdown additionally
suppresses the rows below it and calls `consumePointerClick()` on a choice and on a
dismissal.

**Clipping is also a hit test.** A panel whose content is taller than its dock
pushes a scissor rectangle, and a scissor box the renderer owns cannot answer "is the
pointer in here?". `UiContext::pushClip()` mirrors it for the hit test, and the
inspector clips both. Without the second half, a property scrolled out of view was
still a draggable field sitting under the console.

**Text is measured, never assumed.** A label that does not fit is shrunk before a
single character is dropped — `UiRenderer::textFit` and the launcher's
`drawTextFit`. "Без обмежень" is sixty pixels wider than "Off", and a fixed guess
that suits one ruins the other. Paths are shrunk until they hit the floor and are
then cut from the *front*, because the last component is the one that identifies a
folder.

---

## Docks, zones and the saved workspace

Five panels — hierarchy, inspector, console, favorites, sounds — live in six
places: the left column, the right column, the bottom strip, the two bottom
corners, and free float over the scene. The model is
`Editor::Panel{PanelZone, Rect floatRect, bool open}`, `zoneRect()` produces the
rectangle, and `recomputeLayout()` derives the scene rectangle from **which
columns and rows are occupied** — a floating panel gives its space back.

**The geometry is in `editor/PanelLayout.hpp`, not in the editor.** The zones,
the workspace as numbers (`Workspace`: the sizes the user chose, where every
panel is, whether it is open) and the rectangles they make up are plain data
and plain functions there, and the editor fills a workspace in every frame and
asks them. The reason is `tools/editortest.cpp`: a layout that can only be
checked by opening the editor and looking at it is a layout that is checked by
nobody, and every geometry bug of this editor was found by taking a screenshot
and squinting at it. The numbers can be checked instead.

**Every panel has a place of its own.** The hierarchy has the left column, the
inspector the right one, the console the bottom strip, the favorites the
bottom-left corner and the sounds the bottom-right one. That is Unity's
arrangement, and the reason for it is not decoration: a panel that has no
place is a panel nobody opens, and a panel that is closed is one click away from
being looked at and never is. The corners are where the console stops covering
the side columns, and a corner with a panel in it is space that earns its
place; a corner with nothing in it is a hole.

**The bottom row is three panels and not one row split in three.** The console
and the two corners have heights of their own, and they stand on the bottom of
the window. A row of three panels that share one height is a row of one panel
with two holes in it: dragging the console's edge makes the favorites panel
taller as well, and there is no way to give a corner a height of its own.

**Each column ends where the panel in its own corner begins.** The hierarchy is
above the favorites panel, the inspector is above the sounds one, and the scene
is above the console. The columns are not tied to the tallest panel of the row:
a row that had one height for all three made the favorites panel reach up and
shorten the inspector on the other side of the window, and made the sounds panel
do the same to the hierarchy. A grip moves the thing it is on and nothing else.

**A closed panel reserves nothing.** Closing the console hands its space to the
scene, and closing a corner panel lets the column above it grow into the corner.

**The scene is drawn in the viewport, and the viewport is the scene's own
column.** The picture and the pointer are the same view of the world: the
projection is the camera's own field of view over the viewport, so the horizon
sits where the camera looks and not somewhere under a panel.

**Why the corners are their own zones.** The bottom-left and bottom-right corners
are where the console stops covering the side columns. With one bottom strip, a
panel that owns it leaves a hole on both sides of it, and there is nowhere to put a
second list once the first one has the strip. So the bottom row is three zones and
the whole layout is a grid of two columns and two rows:

```
+---------+---------------+---------+
|  Left   |    Bottom     |  Right  |
+---------+---------------+---------+
|BottomLeft|    Bottom     |BottomRight
+---------+---------------+---------+
   left          middle          right
  column          strip          column
```

Each of the three at the bottom stands on the bottom of the window with a height
of its own, and each of the two columns ends where the panel in its corner
begins - so the three of them tile the window without a hole between them.

- **Drag by the title bar** (six dots — `drawPanelChrome`, shared by all five). Drop
  it on another zone and whoever was there swaps into the zone it came from
  (**a swap, not an eviction**); drop it over the scene and it becomes a floating
  window with a border and a shadow, drawn over the docked ones and not drawn twice.
- **A docked panel's inner edge is its resize grip** (`handlePanelDrag`, holding
  `leftWidth_`/`rightWidth_`/`bottomHeight_` and the two corner heights). A
  floating panel is resized from any
  of its four edges and any of its corners (`panelGripsAt` → a
  `kGripLeft|kGripRight|kGripTop|kGripBottom` mask), and its rectangle is clamped
  into the window so a floating window cannot get lost off screen.
- **What a grip moves is decided by the grip, not by the zone.** A panel in the
  bottom-left corner has two: its top edge makes it taller, and its right edge
  is the edge of the column it shares with the panel above it.
- **Dock sizes live in `Editor`, not in `theme`.** A theme is a palette; what the
  user dragged for themselves is not the theme's to overwrite.
- **`zoneAt()` and `zoneRect()` must agree.** The drop preview is drawn from
  `zoneRect(dropZone_)` and the result is `zoneRect(panel.zone)`, so the column
  edges the hit test uses are the dock widths themselves. `zoneRect()` also counts
  the zone being dragged towards as occupied — otherwise the bottom-left corner
  would be previewed as a rectangle of zero width.

Three things about the size to know before touching `handlePanelDrag`:

- **The grip is tested before the title bar, not after.** The bottom row's only grip
  *is* the first pixels of its title bar. Tested the other way round, a press on the
  console's border picked the panel up and dragged it away instead of making it
  taller - the console was immovable in principle while the other two edges worked,
  and the breakage looked random. The title of a bottom-row panel is therefore
  drawn a few pixels lower than in the other panels: the grip is the top of the
  panel, and a title under the grip is a title that cannot be grabbed.
- **The grip is taken on the press, not on hover.** Otherwise a drag that started in
  the body of a panel would cross its divider and jump its size on the last frame.
  The start rectangle is captured at the press (`resizeStartRect_`) and every frame
  after it is a delta from there.
- **A press is a press that follows a release.** The button can already be down
  when the editor appears - the click that opened the project is the launcher's,
  and the mouse is still held - and a press there is the second half of somebody
  else's click. It used to pick a panel up and drag it to the end of its track,
  and then write the workspace out with the panel at its smallest: the user's own
  layout, changed by the editor, with nothing to have done with it. The editor
  arms itself only once it has seen the pointer up (`pointerArmed_`), and every
  press it acts on goes through `pressAccepted()`.
- **A window that changes size cancels the drag.** The pointer's position in the
  framebuffer moves with the window, and a drag that went on would take that
  movement for the user's own and move a panel by it. The hand did not do it, so
  the hand is not asked to finish it.
- **A grip belongs to one edge of one panel.** The hit test asks for the other
  direction too: the console's top edge is its grip only where the console is, and
  not anywhere along that line of the screen. Without the other direction the
  console lit up its whole top edge while the pointer was over the inspector, and
  the line came and went in pieces as the pointer crossed the panels.
- **The line that shows where a panel can be resized is a line.** One rectangle
  along the whole edge, at a whole number of pixels - and the same for the marks
  that are lines too: the X that closes a panel and the corner of a floating one
  are strokes of squares that touch, not a scatter of dots that only look like
  the shape when you know what it is.
- **A floating panel's top edge is its title bar.** It is taken only in the corner
  boxes and is a drag handle everywhere else, because a grip that ate half the title
  bar would make it not one.
- **A window is dragged by the window.** A floating panel is a surface: the guard says
  that a press on it belongs to it, so that a click on a window does not also pick the
  body behind it. The drag of its title bar is a press of that window, and the drag
  asks the guard **as the window does** (`UiContext::pointerPressedBy(owner)`) - asked as
  anybody else, the window takes the press away from itself, the drag never starts,
  and no floating window can be moved at all. The panel is therefore found **before**
  the guard is asked: the grip or the title bar under the pointer says which panel the
  press belongs to, and the guard is asked as that panel.
- **The release that ends a drag is the drag's own.** The window being dragged is under
  the pointer by definition, and the guard must not swallow the release: the drag would
  go on for ever and the window would follow the pointer after the hand had let go of
  it. The popup capture is still honoured - a dropdown that opened during the drag owns
  the pointer.
- **The window under the pointer is the window on top.** The floating panels are drawn
  in one order and declared into the guard in the same order (`floatingPanelOrder`), so
  the last one drawn is the last one declared, and the last one declared is the one
  that owns the press. The window the pointer is on is drawn last, and a window that is
  under another one cannot be reached.
- **The grip of a floating window belongs to that window.** Its edge is an edge of the
  panel and not a line across the screen: a grip that answered to the whole line
  resized a window that was somewhere else entirely.

A panel is closed with **X** in its header — the same rectangle in drawing and in
the drag test, so the click that closes it cannot also move it, and the X rectangle
is excluded from the grips, or closing the console would resize it — and comes back
from the **View** menu with a checkmark. Closed panels do not occupy a zone:
`zoneRect()` and `recomputeLayout()` look at `open`, so the space goes back to the
scene. A reopened panel returns where it was; if something else is already there it
floats, because two panels never share a zone.

**Favorites** is a star in a hierarchy row (`RigidBody::favorite`, an editor mark
next to `visible`/`locked`), a star in the panel header that narrows the tree to
favorites, and its own panel (`panels/FavoritesDock.cpp`) listing the starred bodies
— the same gesture in the same column. The star sets `sceneDirty_` because it is
scene data written into `scene.txt`, and it is read as an optional field, so scenes
saved before there were stars open as they are.

**A LANDFORM IS MADE OF NOISE.** The Landform panel says how low and how high
the ground goes, how big it is and how finely it is cut into squares, and the
button makes it: the height of every square comes from a value noise, so the
same seed is the same ground on every machine. The squares are cut in rows and a
row does not depend on the next, so the making is spread over as many threads
as the settings say - one to six. The ground lands in the scene as an ordinary
body, a mesh like any other, so the renderer and the physics do not know or
care that this one was not read from a file.

**A GROUND IS SAVED WITH THE SCENE.** A ground is a mesh the engine made, and
there is no file behind it: the mesh lives in the registry of the running
editor and nowhere else. A scene that names it and is saved would therefore
come back with a box the size of the ground where the ground was - the body
kept its shape and its size, and the mesh it pointed at was not there.

So the ground carries the settings it was made of (`MeshAsset::landform`), and
the scene writes them with the body that stands on them:

```json
{
  "shape": "mesh",
  "mesh": { "name": "landform 4242", "scale": 1 },
  "landform": { "shape": "plateau", "biome": "desert", "seed": 4242, "cells": 96, "...": "..." }
}
```

A ground that has been painted has its heights in the same place, as one line
of letters under `"heights"`. A body of water has a `water` node of its own:
its colour and how much of what is under it shows through it.

**THE EFFECTS ARE SAVED WITH THE SCENE TOO.** The emitters are not a body
and they are not a mesh: they are the places the particles come from, and
they are written into the same file, in the same way, with the whole recipe
in them - because a file that is read by hand should say what the effect is
made of.

```json
{
  "emitters": [
    {
      "position": [0, 1, 0], "direction": [0, 1, 0], "body": -1,
      "continuous": true, "interval": 1, "enabled": true,
      "settings": { "count": 28, "lifetime": 0.9, "blend": "additive", "...": "..." }
    }
  ]
}
```

An emitter that rides on a body says which body it is, and its position and
its direction are in the body own frame - so a nozzle on the back of a thing
sprays out of the back of the thing, and it keeps doing that when the thing
turns round.

When the scene is opened, the ground is made again from those settings, under
the name the file gives it, and the body stands on it. The noise is the same on
every machine, so the ground that comes back is the ground that was saved - and
a build of a game needs nothing else, because the settings travel in the scene
file the build carries. The settings themselves are in
`assets/LandformSettings.hpp` and not next to the ground: a ground is a mesh,
and it is the mesh that carries them.

The noise itself is in `scene/Noise.hpp` and is worth a word: it is a hash of
the integer lattice, smoothed, summed over four octaves. It is a picture and
not a measurement, so the hash behind it is the cheap sine everybody uses and
does not care how it turns out.

**A GROUND LOOKS LIKE GROUND.** The noise alone is a field of bumps, and a field
of bumps is not a piece of land. Four things are done to it, and each of them
is a number in the panel:

- **The material slides downhill.** A square that stands above its neighbour by
  more than the angle of repose gives the difference away, and the eight
  neighbours are asked which way down is the steepest - the diagonals count
  for less, because they are further away. This is what takes the spikes off
  and leaves ridges and valleys.
- **The little bumps.** A noise of its own, much finer than the ground it is
  put on, added after the sliding and the smoothing because both of them would
  take it off again. It is the difference between a hill and a balloon.
- **The valleys are darker.** How much of the ground around a square stands
  above it is measured, and the square is darkened by it: the sky does not
  reach the bottom of a valley, and a ground that is coloured only by its
  height and its steepness is a ground of flat bands that says nothing about
  the shape of the ground under it.
- **The biome.** The colours the ground comes out in - and a biome is nothing
  but colours: the ground is made the same way in every one of them. The
  bottom of the ground is sand, soil, ice or ash, the ground itself has a
  wet and a dry version of it, the steep parts are stone, and the top is snow
  or a cap of stone. Eight of them, and the panel shows a swatch of each.

**THE LANDFORM PANEL IS A WINDOW.** It is a tool window: it floats, it starts
closed, and the View menu brings it back. It is dragged by its title bar and
resized by its edges like any other floating panel, and where it is left is
where it comes back - the layout file keeps it with the rest of the workspace.
It is also where the ground lands: the panel says where the new ground is put
in the world, and the ground that is there is an ordinary body that can be
dragged where it belongs afterwards.

**THE BRUSH PAINTS THE GROUND.** The panel has a brush in it - raise, lower,
smooth, flatten - and a button that puts it in the hand. While the brush is in
the hand, the hand in the viewport paints the ground under it instead of
picking a body out of it.

The ground is a heightfield, so the brush is a circle on the heights: the
middle of it does the most and the rim does the least, and the amount is in
metres a second, so the brush does the same work on a slow machine and on a
fast one. The ground under the ray is found with the box of the ground first -
it is cheap - and then with the heights of it, because a box is not a hill.

**A GROUND THAT WAS PAINTED IS SAVED WITH ITS HEIGHTS.** The settings are the
recipe of the ground, and a ground that has been painted is not what the
recipe makes. So the heights of a painted ground are in the scene file beside
the recipe, as one line of letters: a heightfield is thousands of numbers, and
a file a person reads cannot be thousands of lines of numbers. A ground that
has not been painted has no heights in the file - the recipe is enough - and
the file says so by their absence.

**WATER IS A SURFACE AND NOT A THING.** It is a shape of its own (`Shape::Water`),
it is looked at and not collided with, and it casts no shadow: a flat sheet in
the map of the shadows is a flat sheet that shadows itself. The level it
stands at and how big it is are the position and the size of the body, like
every other body keeps them; what the water has of its own is its colour and
how much of what is under it shows through it, and those two are in the scene
file under `water`. The shader of the water is the scene's own for now - the
water shader takes it over, and the body carries everything it needs.

**THE LOADER READS IN PARALLEL.** A model is a file that takes a while to read,
and reading it touches no GL, so the models a scene names are parsed on as many
threads as the settings ask for and their textures are uploaded afterwards, on
the thread that owns the context. The log is written from those threads too, so
it is safe to write from more than one: the file is written under a lock, and a
message from another thread waits in a queue until the thread that owns the log
takes it.

**The saved workspace.** `EditorLayout.cpp` reads and writes
`<project>/editorlayout.txt`: the dock sizes, each panel's zone, rectangle and
whether it is open, and the favorites-only flag. The file lives in the project, not
in `settings.ini`: two projects are two workspaces, and moving the folder moves the
layout with it. With no project (the editor run on its own) it is written next to
the binary.

- **Read on the first frame** (`handleInput`, after `recomputeLayout()`): a floating
  panel restored onto a window that has since shrunk has to be brought back into it.
  **Written on the frame after a change**, not immediately — what has been drawn is
  what the user saw.
- **Values are validated, not trusted.** Sizes are clamped, unknown keys are
  ignored, and two panels in one zone (only possible in a hand-edited file) send the
  later one floating. A zero-sized rectangle means "this panel has never floated"
  and keeps the size the editor started with.
- **The workspace is the user's, and the editor does not touch it on its own.** It
  is written when the user changed something and not otherwise, and the log says
  both times it was read and both times it was written: a layout that changed
  without anybody asking is a bug with a witness, and the witness is in the log.
- **The file's version says what it was written by.** Version 2 is the layout
  where the favorites and the sounds have corners of their own; a file from
  before that has them floating and closed, because there was nowhere else for
  them to be, and the two keep the places they have now. The other panels are
  read from the file as they always were.
- **Sizes that did not change are not written.** The drag sets `layoutDirty_` and
  `handleInput` writes the file once, which is cheap (200 bytes) and keeps a
  frame-by-frame drag from turning into a file write per frame.

---

## The launcher

The start menu. It owns the whole window while it is the active app state: no
viewport, no editor panels, all input. Layout is the one from the concept sketch — a
tab strip across the top, a sidebar down the left, and one list of project rows
filling the rest. Each row carries its own Open and Delete buttons: there is no
shared "selected project" action bar, because the row you are looking at is the one
you mean.

Its frame contract mirrors `Editor::renderUi()`: `initialize()` once after
`UiRenderer::buildFonts()`, then `update()` once per frame while it is active. It
calls `ctx.beginInput()` and `ui.begin()`/`ui.end()` itself, so the integrator must
not call either around it. The returned `Result` says what happened, and
`OpenProject` fills the path.

**Dropdowns.** A select row is drawn in two halves, and the split is not cosmetic: a
dropdown drawn in one pass is painted where the row is, and every row below it lands
on top — a list with the theme and frame-cap labels showing through it is a list
nobody can click. So the field goes with the rest of the panel and the list goes
after all of it, with a flush.

Twenty-three languages are taller than any settings panel, so the list is clamped to
what is reachable on screen and scrolled, with Up/Down/PageUp/PageDown/Home/End
walking it without a mouse. It opens downwards when it fits and upwards when it does
not: a list that runs off the bottom of the window cannot be reached at all. The
opening press is refused (`selectOpenedFrame_`), because the pointer may already be
sitting on a row — a language that changed the moment the list opened needed no
second click, which is not a feature.

**The application icon** is one of the settings rows, and the list is a *listing* of
`assets/image/icons/*.ico` rather than a hard-coded set of names: dropping another
file in there is all it takes for it to appear. Choosing one repaints the window and
the task bar entry immediately — two files are told apart by looking at them, not by
reading their names.

---

## The application state machine

```
        initialize()
              |
              v
        +-----------+   Result::OpenProject    +-----------+
        | Launcher  | ------------------------> |  Editor   |
        +-----------+                          +-----------+
              |                                    |
        Result::Quit                      closing the window
              |                                    |
              v                                    v
           requestClose()                        return 0
```

- `Application::initialize()` creates the window, loads the language bundles and the
  font, initialises `script::Host` and calls `Launcher::initialize()`, which reads
  `settings.ini`, pushes the projects root and the language into `ProjectLibrary` and
  `Localizer`, and scans the folder. The window icon is applied *after* that,
  because the icon is a setting.
- Each frame, `runLauncherFrame()` either stays in the menu, opens a project
  (`readManifest` → `openProject`) or closes the window.
- `Application::openProject()` clears the `PhysicsWorld`, applies gravity and camera
  from the manifest, loads `scene.txt` (a missing or empty file is a fresh project,
  not an error), creates the `Editor` and hands it the scene path.
- There is no way back to the launcher: `Application::closeProject()` exists and
  nothing calls it. Leaving the editor means closing the window.
- `Application::runEditorFrame()`: camera keys → `Editor::handleInput()` (which owns
  the `1..6` gravity keys too, because a field with the keyboard has it) →
  `consumePlayStarted()` (a one-shot signal) → `runProjectScripts()` during it →
  a fixed `world_.step()` → render.

**"Build" is `stageBuild()`.** The program of a build is the **player** - a
separate program, built next to the editor and copied into the build folder under
the name of the game. A build is a folder that can be copied to another machine and
run from there:

```
<build>/
  <Game>/
    <Game>.exe          the program, named after the game
    SpectraPlayer.dll   the core of the engine
    <Game>_Data/
      boot.spectra      the manifest: where the game is
      icon.ico          the picture of the game
      scenes/           the scenes of the game
      scripts/          the scripts of the game
      assets/           the assets of the game
      plugins/          what the engine brings: its languages, its font, its
                        sounds, its sky and its shaders
```

**The program and the core are together** because that is where Windows looks
for a library, and a program that cannot find its core is a program that does
not start. The program carries the core inside as well, so the copy beside it
is the one a project keeps and the one a build of a game is given.
**The data is in a folder of its own** because that is where the
program looks for it: `<name>_Data` beside the program, and the name of the
folder is the name of the program without the extension.

**`boot.spectra` is the manifest of the game.** It is the project's own manifest
under another name, and it says where the scenes are - `scene=scenes/scene.json`,
relative to the data folder. A built game opens it without asking: the boot file
says which game it is, so the launcher is not in the way - and the program that
opens it is the **player** (`Player::initialize()`), not the editor. The editor
asks which project to open; the player is told.

**`plugins/` is what the engine brings**, and `assets/` is what the game is: the
same two roots the engine knows everywhere else - the game's own assets first,
the engine's after them. The models are the exception in the plugins: a folder of
them is the biggest thing in the assets, and a build that carries all of them
carries the ones nobody will ever open - so only the ones the scene names are
staged, with their pictures. The game's own models travel with the game.

`fs::copyTree()` creates the directories that are missing — including every parent,
because Windows' `CreateDirectoryA` makes only the last level — and replaces a file
that is locked by writing beside it and moving it into place: rebuilding on top of an
existing build has to work, because the first "Build" after the second one is an
overwrite.

---

## When a frame is drawn

The most important rule of the loop: **a frame is drawn when, and only when, there is
something to show.** A window nobody touches does not talk to the GPU at all.

`Application::run()` asks two questions each iteration:

1. `Window::consumeActivity()` — did something happen. This is a peek, not a read:
   the front-edge flags stay in place because the widgets consume them next.
   "Something happened" is an event, a **held** key or button (camera flight, a gizmo
   drag), pointer movement (hover is part of the picture) or a window resize.
2. `isSimulating()` — is the physics running. The only case where the picture changes
   by itself.

If both are "no" and the one-second tick has not come, the loop draws nothing and
waits. The tick exists so a missed invalidation cannot hide for longer than a second.

**An edge lives exactly one frame.** After `present()` the loop calls
`dropUnreadInput()` and clears the flags nobody read: key presses, clicks, text,
scroll, and the event queue. Levels (a held key) are left alone — that *is* "someone
is holding something". The moment is not arbitrary: after the frame, not at the top of
the next iteration, because an event that arrived while the loop was waiting belongs
to the next frame, and dropping it before that frame is drawn loses the keystroke
that opened the project in the first place. Without this, one `Enter` held
`consumeActivity()` at "yes" forever and the editor drew at display rate while drawing
nothing new: **0.9 s of CPU per 8 s** against **0.06 s** after the step.

**`InputEvents` is the ordered form of the same edges.** `keyEdges_`,
`mousePressed_` and `mouseReleased_` are a bool per key and a bool per button, which
answers "is this held" and "was this pressed" correctly and cannot answer "what
happened". A bool has no room for a key **tapped** and released inside one frame: by
the time anyone asks, it is already up, and that is a different thing from having
pressed it. The queue (`include/platform/InputEvents.hpp`) keeps the edges in arrival
order and is filled from the same GLFW callbacks, independently:

- `takeKeyPress()` clears `keyEdges_` and **not** the queue, so a script still sees a
  press the editor already used for a shortcut. A game wants that key too.
- `peekActivity()` consults the queue directly. A key **release** pushes an event and
  sets no edge flag, so without this the window would sleep through the one frame that
  shows the result of the user letting go.
- `GLFW_REPEAT` is not an event. Auto-repeat means "still held", which `keys_` already
  says, and a held key would otherwise queue dozens of identical presses a second.
- A key release is in the queue but sets no edge flag either: "was this key pressed
  this frame" must not answer yes on the frame the user let go of it.
- Mouse **movement** is not an event. One pixel is one GLFW callback, so the queue
  would grow with distance instead of with intent; `mouseDelta()` is the aggregate.
- Capped at `kMaxEvents` (256) per frame and the overflow is **reported** by
  `Window::dropUnreadInput()`, not swallowed: a stuck input device or a window someone
  is hammering should not grow a vector without bound over a long session. The cap
  drops the newest edge, because the most recent one is the one whose absence is
  noticed.

There is no GLFW in `InputEvents.hpp` on purpose, the same argument
`FixedClock.hpp` makes. The callbacks need a window; the ordering, the cap and what
`clear()` does to a frame do not, and `scripttest` exercises those directly.

**Waiting is sleeping, not polling.** `WaitForSingleObject(timer, 0)` does not sleep
at all: the first version of this mechanism ate 95% of a core while the window just
sat there. `waitForFrame()` now waits on `MsgWaitForMultipleObjects` for the timer
**or** a message, so a click is handled immediately and the core sleeps. A
manual-reset waitable timer has to be disarmed with `CancelWaitableTimer` after it
fires, or the next wait returns instantly and the loop spins.

**Waiting wakes on an event, not at the deadline.** During the pause before the tick,
`waitForFrame(window, tickDue, true)` returns as soon as `Window::peekActivity()` sees
movement. Without that the wait ran to the end of the tick and hovering a button lit
up a second later. Hence the two questions in `Window`: `consumeActivity()` records
the pointer position (for the next frame) and `peekActivity()` reads the same thing
without recording — otherwise the movement that woke the wait is eaten and the loop
never sees that anything happened. Between frames there is no waking (`false`): that
frame is coming anyway, so the mouse must not raise the rate above the one that was
asked for.

Measured on the development machine (GT 540M, one-way copper):

| State | CPU over 6 s |
|---|---|
| Menu, idle | 0.00 s |
| Editor, idle | 0.00 s |
| Play, 64 bodies | 0.80 s |
| After Stop | 0.00 s |

---

## On-disk formats

`ProjectLibrary` in `core` is the only owner of the on-disk formats. The file names
are constants in `Project.hpp` so the editor and the scripting layer cannot invent
their own literals.

| Path | What it is |
|---|---|
| `<projectsRoot>/<Project>/project.Spectra` | the manifest: `name`, `version`, `engine`, `created`, `modified`, `language`, `gravity`, `favorite`, `cameraX/Y/Z`, `cameraYaw`, `cameraPitch`, `cameraFov`, `shadowQuality`, `shadowResolution`, `shadowCascades`, `shadowSunAzimuth`, `shadowSunElevation`, `shadowBias`, `shadowNormalOffset`, `shadowRadius`, `scene` |
| `<projectsRoot>/<Project>/scenes/scene.json` | the scene document (an empty file is created with the project) |
| `<projectsRoot>/<Project>/editorlayout.txt` | the saved editor workspace (written by `EditorLayout.cpp`) |
| `<projectsRoot>/<Project>/scripts/` | `*.as`, run when Play begins |
| `<projectsRoot>/<Project>/assets/` | the content of the project: `textures/`, `audio/`, `music/`, `shaders/`, `material/`, `fonts/`, `models/`, `sky/`, `lang/`, `icons/`, `prefabs/`, `ui/`, `data/`, `particles/`, `physics/` |
| `<projectsRoot>/<Project>/lib/` | the core of the engine as a file: `SpectraPlayer.dll` - the core the project was made with, which the program also carries inside |
| `<projectsRoot>/<Project>/icon.ico` | the picture of the project: the icon of the window while the project is open |
| `<gameDir>/<Game>_Data/boot.spectra` | the manifest of a built game: the same keys, and the scene of the game beside it |
| `<exeDir>/assets/sounds/*.wav` | the sounds the engine ships: the name of each is its file's stem, so a file dropped in the folder is a sound the scripts can play and a body can carry |
| `<exeDir>/assets/fonts/JetBrainsMono-Regular.ttf` | the font the interface is drawn with, with its licence beside it. It is shipped rather than borrowed so the engine has the same letters on every machine it runs on |
| `<exeDir>/assets/image/icons/*.ico` | the application icons the settings screen offers |
| `<exeDir>/settings.ini` | machine-level settings: `projectRoot`, `language`, `theme`, `frameRateCap`, `appIcon`, up to 8 `recent` |
| `<exeDir>/engine.log` | the log of the run: stderr and this file are the same lines, and the editor console reads the ring buffer they come from |
| `<exeDir>/crash-001.log` | the report of a crash: the reason, the address of the fault, the stack and the tail of the log. The next crash takes the next free number, so the reports of several crashes are all kept |

The projects root is `Settings::projectRoot`, by default `<exeDir>/project` - and
`<exeDir>/../project` when that is the one that is there, which is where a built
game keeps the projects of the machine it was built on.

Facts worth keeping in mind, because they mildly contradict intuition:

- **The manifest is written once.** `writeManifest()` is called only from
  `ProjectLibrary::create()`, so the camera and gravity keys stay as they were set
  at creation even after the camera has been moved in the editor.
- **A project's `models/` is not scanned.** `AssetBrowser::refresh()` reads the
  shared `assets/models` - the project's own `assets/models` first and the
  engine's after it; the folder is created but filled only by hand or through
  `engine::importModel`. The list feeds the Create menu, is refreshed by the `⟳`
  button in the bar, or by File → Reload.
- **The title bar is icon, engine name, dash, project name**, and nothing else: no
  frame counter. `Application::updateTitle()` rewrites the string only when a project
  is opened or closed.
- **`Settings::load()` returns false on the first run.** That is not an error: the
  absent keys simply leave the defaults.

---

## The scripting layer

- **When.** `Application::runProjectScripts()` reads `<project>/scripts/*.as` (sorted
  by name) and runs each through `Host::runFile()`. The call is triggered by
  `Editor::consumePlayStarted()`, so the scripts see a live world on the first Play
  frame. A missing directory or an empty one is not an error.
- **Entry point.** Exactly one function per module: `onInit` / `main` / `start`,
  otherwise the first global `void()` in declaration order, otherwise nothing (only
  `shared`). "void()" means *no parameters* — a per-frame hook takes `dt`, so it is
  not an entry point and does not run on load.
- **Hooks.** `Host` knows four optional declarations and calls them itself:
  `void onUpdate(float dt)`, `void onFixedUpdate(float dt)`, `void onStop()`, and the
  legacy `void onUpdate()` with no arguments. They are all optional and a script that
  declares none of them is normal, not an error.
  - `onUpdate(float dt)` runs **once per rendered frame**, before the physics
    accumulator — it sees the real frame delta, so movement it writes scales with the
    display's refresh rate.
  - `onFixedUpdate(float dt)` runs **once per fixed step**, inside the accumulator, so
    it always sees the same `dt` regardless of frame rate. That is what makes
    physics-driving script behaviour identical on a 60 Hz and a 144 Hz machine, and it
    is why it is a separate function instead of a flag on `onUpdate`.
  - `onStop()` runs when Play ends, after the edit-mode snapshot is restored, so a
    script can undo whatever it created.
  - The old `void onUpdate()` still works and is only used when no `dt` form is
    declared anywhere; declaring both is reported by the duplicate-owner warning.
- **Step budget.** `FixedClock` is constructed as `{60.0, 2}` — 60 Hz with at most 2
  catch-up steps per frame. `kMaxFrameTime = 0.1` already caps a frame at 6 steps of
  1/60 s, so the previous cap of 10 was unreachable and only meant the `for` loop
  looked unbounded. A long stall therefore slows time down instead of spiralling.
  Since `onFixedUpdate` runs per step, it runs at most twice per frame.
- **Input: state and events, both.** `engine::isKeyDown` / `isMouseButtonDown` /
    `getMouseDelta` / `setMouseCaptured` / `isMouseCaptured` read the live `Window`,
    so a `Host` built with `nullptr` — `scripttest`, anything headless — answers
    `false` and zeroes its `out` arguments instead of dereferencing nothing.
    `eventCount()` / `eventType()` / `eventCode()` / `eventValue()` walk the frame's
    edges; see `InputEvents` below for why both exist.
  - `getScrollDelta()` reads the window's accumulated scroll, which `Application`
    consumes earlier in the frame for the editor. In practice a script sees `0`.
- **Diagnostics.** `Host::setDiagnosticSink()` is installed before `initialize()`;
  compiler and exception messages go to `Log` with the file, line and column
  (`[script] ...` for `engine::log*`).
- **One host.** AngelScript's global functions have no receiver, so `ScriptState` is
  published through `activeState()`, and a second `Host` refuses to initialize.
- **Building without scripting.** Every `src/script/*.cpp` is guarded by
  `SPECTRA_SCRIPTING`; the headers contain no AngelScript symbol at all, so
  `-DSPECTRA_SCRIPTING=OFF` simply leaves the engine off (`Host::available()` is
  false, and the startup banner says `Scripting: off`).

---

## Defects this rewrite fixed

These existed in the code before the refactor:

- **The arena floor generated no contacts.** The contact generator skipped the wall
  gravity pointed at, so bodies fell through it. There are no walls at all now:
  the world has no box around it, and a game decides where its own bounds are.
  What falls out of the world keeps falling.
- **Warm-start impulses lived forever.** The cache keyed by (wall, corner) was never
  cleared when a contact went away, so a body that bounced and fell back in the same
  place got a shove from the previous impact and the bounces grew without bound. The
  impulses of a pair live in a table keyed by the pair, and a pair that is gone has
  no row.
- **Micro-jitter at rest.** A contact was created only on penetration, so a resting
  body fell and re-took an impulse every step and never slept. Contacts are now
  speculative (a 2 cm gap) and the solver does not touch surfaces that cannot close
  within the step.
- **The popup click-through.** A click on a row of a dropdown also reached the button
  the row happened to cover, because the popup is drawn after the widgets under it.
  Fixed by the pointer capture in [the UI frame contract](#the-ui-frame-contract).
- **Console text through floating windows.** The batcher emits rectangles before
  text, so without a flush between the docked panels and the floating ones a floating
  window's background landed under the console's log lines. Fixed by the staged
  flushes.
- **The bottom corners were dead space.** With a single bottom strip, the corners
  beside it belonged to no panel at all. Fixed by the corner zones.
- **The Linux build was broken:** `src/Lang.cpp` was not in the file list.

---

## How it is verified

The build is clean at `-Wall -Wextra` (add `-DSPECTRA_WERROR=ON` to make that a
build failure rather than a note).

The scripting layer is checked headlessly by `scripttest` (46 checks,
`tools/scripttest.cpp`): an entry point beside a helper runs exactly once, a missing
hook returns `false` **silently**, a present one runs, a compile error produces an
ERROR diagnostic, and a `shared`-only script compiles without complaint. `ctest` runs
it when the tools are built.

The editor's panel geometry is checked headlessly by `editortest` (45 checks,
`tools/editortest.cpp`): the window is tiled, a closed panel reserves nothing, a
grip moves the thing it is on and nothing else, a grip is on the edge of its own
panel and not on a whole line of the screen, the picture and the pointer agree
about where a thing is, and the font the engine ships has every letter the atlas
draws. It needs no window and no editor - the geometry is plain functions over
plain data, which is why it can be asked about here.

The cloud is checked there as well: that it is made, that the same seed is the
same cloud and another seed is another one, that the threads do not change it,
that fewer puffs and a coarser ball make a cheaper cloud, that the flat cloud
is a picture on a plane and the cloud of puffs is not, that the top of the
cloud is brighter than the bottom of it, and that a cloud that is saved comes
back a cloud and not a box in its place.

It has already paid for itself: the first run of it found that the atlas's slot
table counted 96 ASCII characters where there are 95, and spent the last slot
on U+007F, which no font draws and nothing ever asked for.

`tools/ui_language_test.ps1` drives the real `spectra3d.exe` with injected Windows
input and checks, in order: opening the language dropdown does **not** change the
language, hovering a row does not, scrolling does not, clicking a row does, and
reopening changes nothing. A screenshot cannot catch that class of bug — it shows the
dropdown open, which looks correct — only `settings.ini` before and after can. The
clicks are computed from the constants in `Launcher.cpp` rather than from a fraction
of the window, because a guess that lands on one machine and misses a 28-pixel gear
on the next is a coincidence, not a test.

Beyond that, scripts in the log were used to exercise a sphere falling and sleeping, a
stack of boxes staying still, a sphere bouncing off a box, all six gravity axes,
ray-picking, a save/load cycle of a scene (including importing a real model from
`assets/models/`) and the volume/inertia formulas.
