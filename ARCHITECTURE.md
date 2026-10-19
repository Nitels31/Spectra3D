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
              app        Application, Launcher, FixedClock, main
                |
              editor      Editor, panels/, Picking, Gizmo, AssetBrowser
                |
              script      ScriptHost, BindEngine/BindScene/BindCamera/BindString/BindInput
                |
              scene       BodyFactory, SceneSerializer
                |
                ui         Theme (palettes), UiContext
                |
              render      Camera, Color, SceneRenderer, UiRenderer,
                |          TextAtlas, PrimitiveCache, GlInfo
              assets      MeshAsset, MeshRegistry, MeshImport, MeshUtil, loaders/
                |
              physics     RigidBody, Material, PhysicsWorld
                |
             +------+------+
             |             |
         platform        i18n       Window, Filesystem      Localizer
             |                         (core + platform)
             +------+------+
                    |
                   core     Math, Log, Str, Project

   third_party/     glfw, freetype, angelscript, miniaudio,
                models_lib/ (cgltf, rapidobj, miniply, ufbx),
                stb/ (stb_image, stb_vorbis), json/
```

`ui` sits above `render` because `UiContext.hpp` takes `Rect` and `Color` from
there, while `Theme.hpp` is only colours. `script` sits above `scene` —
`BindEngine.cpp` creates bodies through `BodyFactory` — and below `editor`, because
`Application` runs it alongside the `MeshRegistry` and the camera. `GlInfo.hpp`
lives in `render` because it is about the context, and it reads `kGlVersionMajor`
from `platform/Window.hpp`: a dependency upward, which is exactly where it belongs.

## Rules that hold the structure together

1. **One direction.** `physics` does not know about `assets`; `render` does not know
   about `editor`; `editor` does not know about `app`. A violation is visible
   immediately: the function you are writing belongs a layer up.

2. **No globals, except the log.** Everything lives in an instance somebody owns.
   The single exception is `s3d::Log`, which every layer reads — the model loaders
   included — and threading an instance of it through every call stack buys nothing.
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

7. **Two targets, and the boundary is the layer graph.** `spectra_runtime` is a
   static library holding every layer below `app/` and `editor/`. `spectra3d` is
   the shell: `Application`, `Launcher`, `main` and the editor, linking the
   runtime. The split is not cosmetic - it is what lets a second executable be
   "the same engine, an empty window and a loop", which is what Build already
   promises, and it is what stops the editor and that executable from drifting
   apart. Nothing below `app/` or `editor/` includes either of them (the one
   exception is `Application.hpp`, which owns both).

   `tools/scripttest` used to name eighteen engine sources by hand. It now links
   `spectra_runtime`, because a source list is not a dependency list: the old one
   could not see a file added to the engine until something stopped linking.

---

## The layers

### `core`

`Math.hpp` (`Vec3`/`Quat`/`Mat3`, `clampf`, `lerpf`), `Log` (stderr + `engine.log` +
the ring buffer the editor console reads), `Str` (UTF-8, paths, extensions) and
`Project` (`Settings`, `ProjectLibrary`, `ProjectInfo`, the project file-name
constants). Nothing above the standard library.

`Project.cpp` is the one place `core` allows itself `#include
"platform/Filesystem.hpp"`: enumerating directories and finding the executable are
both needed to scan projects and to read and write files. It is a deliberate,
named exception rather than a habit.

`Settings` is machine-level and lives next to the binary (`settings.ini`), so the
installation is portable. `ProjectInfo` is project-level and lives in the project
directory, so a project folder is a complete unit.

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

### `physics`

`RigidBody` (mass, inertia, bounds, warm-start cache, `Material`) and
`PhysicsWorld` (gravity, contact generation, solver, sleeping). It knows nothing
about models: a body stores a mesh *index*, which the renderer resolves.

### `assets`

`MeshAsset`, `MeshRegistry` (the owner of every imported model — there is no global
registry any more), `MeshUtil` (shared bounds/normals/size fitting) and one file per
format in `loaders/`.

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

The sky is drawn first, with the depth test off and the depth write off, so it
is behind everything and hides nothing. The cube is put where the camera is and
scaled up, and it is **not turned**: the model matrix is a translation and a
scale and nothing else, so the cube's own frame is the world's frame, and the
direction a texel is looked up in is a world direction.

**The grid follows the camera.** The arena grid is a *window on the world
grid*: the patch is snapped to the major step and moved with the camera, so
every line lands back on the world position it was on before and there is no
edge of the grid to walk to. It is drawn in two parts, because one part cannot
be both fine and endless - the minor lines are one unit apart and are only
worth drawing near the camera, and the major lines are ten units apart and
carry the grid out to the horizon, where one unit is smaller than a pixel and
would only alias.

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

### `scene`

`BodyFactory` (every way a body gets created) and `SceneSerializer` (the text scene
format, with models referenced by name).

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
`PhysicsWorld`/`MeshRegistry`/`Camera` plus the groups scripts create) and the
registration tables; `BindEngine.cpp` is logging, spawning, gravity and body access;
`BindScene.cpp` is the scene, model import and groups; `BindCamera.cpp` is the camera;
`BindInput.cpp` is the keyboard and mouse, read from the live `Window`;
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
list lives in `AssetBrowser`. `Editor::handleHotkeys()` is empty on purpose: play,
pause, stop, save, load and delete live on buttons and menu rows, because every one
of them mutates or discards the scene and a stray keypress while typing used to throw
work away.

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

`Application` is the composition root: it owns every subsystem, runs the frame loop
and switches between the two app states. `Launcher` is the start menu, which lives
in `Launcher::Result { Stay, OpenProject, Quit }`. `FixedClock` is the fixed-step
accumulator (60 Hz). `main.cpp` is five lines.

`Application` and `Launcher` sit **in the executable, not in `spectra_runtime`**,
which is rule 7 above: both are the shell around the engine, not the engine. The
consequence is visible in the file list - the runtime stops at `i18n`.

**The step cap and the frame-time clamp are two different brakes.**
`kMaxFrameTime` (`Application.cpp`) clamps `dt` to 0.1 s, so at 60 Hz a fully
clamped frame can fund at most **six** 16.7 ms steps. `FixedClock`'s cap is a
second, independent brake, and the header argues for it at length: with the cap
binding first, frame time is `cap * step + render` and stays proportional to the
real cost; without it, a slow step is paid up to six times in a row and the frame
rate pins itself to a plateau it cannot leave. **A cap above six never binds**,
which makes it a second value that has to agree with the first.

---

## The UI frame contract

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

Four panels — hierarchy, inspector, console, favorites — live in six places: the
left column, the right column, the bottom strip, the two bottom corners, and free
float over the scene. The model is `Editor::Panel{PanelZone, Rect floatRect, bool
open}`, `zoneRect()` produces the rectangle, and `recomputeLayout()` derives the
scene rectangle from **which columns and rows are occupied** — a floating panel
gives its space back.

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

- **Drag by the title bar** (six dots — `drawPanelChrome`, shared by all four). Drop
  it on another zone and whoever was there swaps into the zone it came from
  (**a swap, not an eviction**); drop it over the scene and it becomes a floating
  window with a border and a shadow, drawn over the docked ones and not drawn twice.
- **A docked panel's inner edge is its resize grip** (`handlePanelDrag`, holding
  `leftWidth_`/`rightWidth_`/`bottomHeight_`). A floating panel is resized from any
  of its four edges and any of its corners (`panelGripsAt` → a
  `kGripLeft|kGripRight|kGripTop|kGripBottom` mask), and its rectangle is clamped
  into the window so a floating window cannot get lost off screen.
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
  taller — the console was immovable in principle while the other two edges worked,
  and the breakage looked random.
- **The grip is taken on the press, not on hover.** Otherwise a drag that started in
  the body of a panel would cross its divider and jump its size on the last frame.
  The start rectangle is captured at the press (`resizeStartRect_`) and every frame
  after it is a delta from there.
- **A floating panel's top edge is its title bar.** It is taken only in the corner
  boxes and is a drag handle everywhere else, because a grip that ate half the title
  bar would make it not one.

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
- `Application::runEditorFrame()`: camera keys and `1..6` for gravity →
  `Editor::handleInput()` → `consumePlayStarted()` (a one-shot signal) →
  `runProjectScripts()` during it → a fixed `world_.step()` → render.

**"Build" is `stageBuild()`.** There is no separate player: the player *is* this
executable, so a build is the program, the DLLs beside it, `assets/` and the project
itself. The project is copied to `<build>/project/<name>`, because that is where the
built program will look for projects when it starts, exactly as the editor does. It
is copied **entry by entry**, not as a tree: the usual build folder lives *inside*
the project, and a tree copy would copy the build into itself, doubling it on every
rebuild.

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
| `<projectsRoot>/<Project>/project.spectra` | the manifest: `name`, `version`, `engine`, `created`, `modified`, `language`, `gravity`, `favorite`, `cameraX/Y/Z`, `cameraYaw`, `cameraPitch`, `cameraFov` |
| `<projectsRoot>/<Project>/scene.txt` | the scene document (an empty file is created with the project) |
| `<projectsRoot>/<Project>/editorlayout.txt` | the saved editor workspace (written by `EditorLayout.cpp`) |
| `<projectsRoot>/<Project>/scripts/` | `*.as`, run when Play begins |
| `<projectsRoot>/<Project>/models/` | the project's own model folder |
| `<exeDir>/assets/image/icons/*.ico` | the application icons the settings screen offers |
| `<exeDir>/settings.ini` | machine-level settings: `projectRoot`, `language`, `theme`, `frameRateCap`, `appIcon`, up to 8 `recent` |

The projects root is `Settings::projectRoot`, by default `<exeDir>/project`.

Facts worth keeping in mind, because they mildly contradict intuition:

- **The manifest is written once.** `writeManifest()` is called only from
  `ProjectLibrary::create()`, so the camera and gravity keys stay as they were set
  at creation even after the camera has been moved in the editor.
- **A project's `models/` is not scanned.** `AssetBrowser::refresh()` reads the
  shared `assets/models`; the folder is created but filled only by hand or through
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
  gravity pointed at, so bodies fell through it. All six walls take part now —
  `PhysicsWorld::collectWallContacts()`.
- **Warm-start impulses lived forever.** The cache keyed by (wall, corner) was never
  cleared when a contact went away, so a body that bounced and fell back in the same
  place got a shove from the previous impact and the bounces grew without bound. A
  `warmStamp` now invalidates surfaces that are not in this step's contact set.
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
