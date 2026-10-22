cmake_minimum_required(VERSION 3.16)

# include/core/Project.hpp is the single source of truth for both: the version the
# launcher, the manifest and the title bar report, and the folder the application
# icons live in. Reading them here means the build can never disagree with the
# engine about either.
#
# The whole file is read, and not the first few kilobytes of it: a LIMIT stops
# being a shortcut and starts being a place where a constant can fall off the
# end - the icon directory moved past it when the file grew, and the build
# failed with "cannot find kAppIconsDir" in a header that plainly has it.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/include/core/Project.hpp" SPECTRA_PROJECT_HEADER)
string(REGEX MATCH "kEngineVersion[ \t]*=[ \t]*\"([0-9]+\\.[0-9]+\\.[0-9]+)\""
       SPECTRA_VERSION_MATCH "${SPECTRA_PROJECT_HEADER}")
set(Spectra3D_VERSION "${CMAKE_MATCH_1}")
string(REGEX MATCH "kAppIconsDir[ \t]*=[ \t]*\"([^\"]+)\""
       SPECTRA_ICON_DIR_MATCH "${SPECTRA_PROJECT_HEADER}")
set(SPECTRA_ICON_DIR "${CMAKE_MATCH_1}")
if(NOT Spectra3D_VERSION OR NOT SPECTRA_ICON_DIR)
    message(FATAL_ERROR
        "Cannot find kEngineVersion / kAppIconsDir in include/core/Project.hpp")
endif()
unset(SPECTRA_PROJECT_HEADER)
unset(SPECTRA_VERSION_MATCH)
unset(SPECTRA_ICON_DIR_MATCH)
# kAppIconsDir already includes "assets/" - it is the path relative to the
# executable, which is what Window::loadShippedIcon appends to the module path.
# Prepending "assets/" here too made the path assets/assets/..., which does not
# exist, so the icon was silently never embedded in the .exe.
set(SPECTRA_ICON "${CMAKE_CURRENT_SOURCE_DIR}/${SPECTRA_ICON_DIR}/icon.ico")

project(Spectra3D VERSION ${Spectra3D_VERSION} LANGUAGES C CXX)

# Single source of truth for the build. build_windows.bat / build_linux.sh are thin
# wrappers around this file.
#
# Layout the targets rely on:
#   src/        implementation (.cpp), one layer per directory
#   include/    every header the engine owns, one layer per directory, so a header
#               is reached as "layer/Name.hpp" from anywhere
#   third_party/ vendored libraries, one target each, below
#   assets/     languages, images and models, staged next to the binary
#   tools/      developer executables, off by default

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# One binary per build tree, in build/ itself, for single- and multi-config
# generators alike - otherwise a Visual Studio build drops it into build/Debug.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} ${CMAKE_BINARY_DIR})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config} ${CMAKE_BINARY_DIR})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config} ${CMAKE_BINARY_DIR}/lib)
endforeach()

# Options
# -------
# SPECTRA_BUILD_TOOLS   developer executables (glbinfo, fbxtest, scripttest). Off by
#                       default so a normal build produces exactly one executable -
#                       the engine.
# SPECTRA_SCRIPTING     the AngelScript runtime. Off drops src/script/*.cpp entirely
#                       and leaves the editor without scripting rather than half
#                       wired to it.
# SPECTRA_WERROR        warnings become errors. Off by default so a new compiler
#                       version does not break everyone's build on a warning.
# SPECTRA_LTO           link-time optimisation. The compiler sees across the
#                       libraries, and the linker throws away what nobody uses.
# SPECTRA_NATIVE        build for the processor of the machine that builds it.
#                       Off by default: a build that only runs on the machine
#                       that made it is a build that does not travel.
option(SPECTRA_BUILD_TOOLS "Build the standalone asset inspection tools" OFF)
option(SPECTRA_SCRIPTING "Build the AngelScript runtime" ON)
option(SPECTRA_WERROR "Treat compiler warnings as errors" OFF)
option(SPECTRA_LTO "Link-time optimisation" ON)
option(SPECTRA_NATIVE "Build for this machine's processor" OFF)

set(SPECTRA_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(SPECTRA_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
set(SPECTRA_ASSETS ${CMAKE_CURRENT_SOURCE_DIR}/assets)

# Warning flags, in one place, so every target below gets the same set. Applied by
# spectra_warnings() rather than copied into each target: a new target that forgets
# them is a target nobody checks.
function(spectra_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W3 /permissive- /utf-8)
        if(SPECTRA_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
        if(SPECTRA_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# The optimisation flags, in one place, so every target below gets the same
# set. Applied by spectra_optimise() rather than copied into each target: a
# new target that forgets them is a target that is built slowly.
#
# Release is already -O3. These are the flags that make that -O3 worth
# having:
#
#  - **the link-time optimisation**, so the compiler sees across the
#    libraries instead of guessing at them, and the linker throws away what
#    nobody uses. It is the biggest single thing in this list: the engine is
#    a hundred files and every one of them calls into the others.
#  - **the sections**, so the linker has something to throw away. A function
#    in a section of its own is a function the linker can drop; a function in
#    the big one is a function everybody keeps.
#  - **-fno-math-errno**, so the roots and the powers are not obliged to set
#    errno, and the compiler may do to them what it can. Nothing in the
#    engine reads errno after a root.
#  - **the march**, when the build is for one machine and not for all of them.
#
# The link-time part is only in Release. A Debug build with it is a build
# that cannot be stepped through, and the step is the point of a Debug build.
function(spectra_optimise target)
    if(MSVC)
        # /GL is the link-time part and /LTCG asks the linker for it. Both are
        # in Release and in both places: /GL without /LTCG is a build that
        # warns about itself, and /LTCG without /GL does nothing.
        target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:/GL>)
        target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:/Gy>)
    else()
        target_compile_options(${target} PRIVATE -fno-math-errno)
        target_compile_options(${target} PRIVATE -ffunction-sections -fdata-sections)
        if(SPECTRA_NATIVE)
            target_compile_options(${target} PRIVATE -march=native)
        endif()
    endif()
    if(SPECTRA_LTO AND NOT MSVC)
        target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:-flto=auto>)
    endif()
endfunction()

# The link flags, for the things that are actually linked: the core as a file
# and the two programs. A static library is not linked, so the flags go on
# the programs that link it.
function(spectra_link_optimise target)
    if(MSVC)
        target_link_options(${target} PRIVATE $<$<CONFIG:Release>:/LTCG>)
        target_link_options(${target} PRIVATE /OPT:ICF /OPT:REF)
    else()
        target_link_options(${target} PRIVATE -Wl,--gc-sections)
        target_link_options(${target} PRIVATE -Wl,-O1)
        target_link_options(${target} PRIVATE -Wl,--as-needed)
        if(SPECTRA_LTO)
            target_link_options(${target} PRIVATE $<$<CONFIG:Release>:-flto=auto>)
        endif()
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Third-party
# ---------------------------------------------------------------------------
#
# Each vendored library is one target with PUBLIC includes, so nothing above has
# to repeat a -I for it and the include path of a dependency can only change in
# the place that owns it. SYSTEM is on the third-party includes: a warning about a
# header that is not ours is a warning nobody can fix.

# FreeType: only the modules the text atlas needs, not the whole library. Font
# files, compressed streams and the raster back ends are dead weight here, and they
# are most of FreeType's build time.
add_library(spectra_freetype STATIC
    third_party/freetype/src/autofit/autofit.c
    third_party/freetype/src/base/ftbase.c
    third_party/freetype/src/base/ftbbox.c
    third_party/freetype/src/base/ftbitmap.c
    third_party/freetype/src/base/ftdebug.c
    third_party/freetype/src/base/ftglyph.c
    third_party/freetype/src/base/ftinit.c
    third_party/freetype/src/base/ftsystem.c
    third_party/freetype/src/base/ftmm.c
    third_party/freetype/src/lzw/ftlzw.c
    third_party/freetype/src/truetype/truetype.c
    third_party/freetype/src/type1/type1.c
    third_party/freetype/src/cff/cff.c
    third_party/freetype/src/cid/type1cid.c
    third_party/freetype/src/pfr/pfr.c
    third_party/freetype/src/type42/type42.c
    third_party/freetype/src/winfonts/winfnt.c
    third_party/freetype/src/pcf/pcf.c
    third_party/freetype/src/bdf/bdf.c
    third_party/freetype/src/psaux/psaux.c
    third_party/freetype/src/psnames/psnames.c
    third_party/freetype/src/pshinter/pshinter.c
    third_party/freetype/src/sfnt/sfnt.c
    third_party/freetype/src/smooth/smooth.c
    third_party/freetype/src/raster/raster.c
    third_party/freetype/src/sdf/sdf.c
    third_party/freetype/src/svg/svg.c
    third_party/freetype/src/gzip/ftgzip.c
)
target_include_directories(spectra_freetype SYSTEM PUBLIC third_party/freetype/include)
target_compile_definitions(spectra_freetype PRIVATE FT2_BUILD_LIBRARY)
# Not our code and not our warnings: -w rather than a level, so a real diagnostic
# in FreeType itself is not buried in ours.
if(NOT MSVC)
    target_compile_options(spectra_freetype PRIVATE -w)
    # FreeType is compiled with the link-time optimisation as well, and that is
    # not for the speed of it.
    #
    # A library that is compiled without it is a library whose members the
    # linker has to guess about: it takes the member that holds a symbol and
    # stops there, and it does not always come back for the member that holds
    # a symbol another member of the same library asks for. The raster module
    # of FreeType asks the debug module for the trace functions, and whether
    # the debug module is in the link depends on the order the linker happens
    # to meet the two in - which is not a thing to leave to chance. With the
    # optimisation on, the whole of the library is one picture, and the linker
    # cannot leave half of it out.
    if(SPECTRA_LTO)
        target_compile_options(spectra_freetype PRIVATE $<$<CONFIG:Release>:-flto=auto>)
    endif()
endif()

# Model loaders, all four under models_lib: ufbx for FBX, miniply for PLY, cgltf
# and rapidobj straight from their headers where a target would carry no compiled
# code. One folder for "things that turn a model file into vertices", so adding a
# fifth loader does not mean another top-level directory.
add_library(spectra_ufbx STATIC third_party/models_lib/ufbx/ufbx.c)
target_include_directories(spectra_ufbx SYSTEM PUBLIC third_party/models_lib/ufbx)

add_library(spectra_miniply STATIC third_party/models_lib/miniply/miniply.cpp)
target_include_directories(spectra_miniply SYSTEM PUBLIC third_party/models_lib/miniply)

# Header-only dependencies get an INTERFACE target rather than a bare -I. There is
# nothing to compile, only a directory to add, but the name keeps the include in
# the same shape as every other dependency's.
add_library(spectra_gltf INTERFACE)
target_include_directories(spectra_gltf SYSTEM INTERFACE third_party/models_lib/cgltf)

add_library(spectra_obj INTERFACE)
target_include_directories(spectra_obj SYSTEM INTERFACE third_party/models_lib/rapidobj)

# nlohmann/json, vendored as a single header under third_party/json. Reached as
# "json/json.hpp" - the include directory is third_party itself, so the header
# keeps its folder name as its first path component exactly as it did while it
# lived under include/.
add_library(spectra_json INTERFACE)
target_include_directories(spectra_json SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# stb_image, for reading PNG/JPEG/BMP into GL. The vendored copy is under
# third_party/stb/stb_image, reached as <stb_image.h>; one include directory for
# the family, matching how the libraries are laid out.
add_library(spectra_stb INTERFACE)
target_include_directories(spectra_stb SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb/stb_image
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb/stb_image_write)

# miniaudio, for the sound. One header with the implementation in it, so only
# the one file that defines MA_IMPLEMENTATION ever sees it.
add_library(spectra_miniaudio INTERFACE)
target_include_directories(spectra_miniaudio SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio)

# ---------------------------------------------------------------------------
# Scripting
# ---------------------------------------------------------------------------

if(SPECTRA_SCRIPTING)
    # GLOB rather than a list: the SDK is around a hundred files and the set only
    # changes when the vendored version does. CONFIGURE_DEPENDS re-runs the glob
    # when the directory does, so a new file is picked up on the next build.
    file(GLOB SPECTRA_ANGELSCRIPT_SOURCES CONFIGURE_DEPENDS
         ${SPECTRA_THIRD_PARTY}/angelscript/source/*.cpp)
    add_library(spectra_angelscript STATIC ${SPECTRA_ANGELSCRIPT_SOURCES})
    target_include_directories(spectra_angelscript
        SYSTEM PUBLIC third_party/angelscript/include
        PRIVATE third_party/angelscript/source)
    if(NOT MSVC)
        target_compile_options(spectra_angelscript PRIVATE -w)
    endif()
endif()

# ---------------------------------------------------------------------------
# The engine
# ---------------------------------------------------------------------------

# The application icon goes into the executable as well, so the .exe carries it in
# Explorer and in the task bar before the window exists. That needs a resource
# compiler, which not every toolchain has, so it is optional: where there is none
# the window still gets the icon at runtime, and the file is staged next to the
# binary either way.
set(SPECTRA_ICON_RC "")
if(WIN32 AND EXISTS "${SPECTRA_ICON}")
    include(CheckLanguage)
    check_language(RC)
    if(CMAKE_RC_COMPILER)
        enable_language(RC)
        # Generated rather than kept in the tree: the only thing in it is the path
        # to the icon, and a path belongs to the machine that built it.
        #
        # RELATIVE, not absolute: windres silently produces an empty object file
        # for an absolute Windows path, and the .exe then has no icon at all with
        # no warning from anyone. Relative to the build directory, which is where
        # the .rc is compiled.
        file(RELATIVE_PATH SPECTRA_ICON_REL "${CMAKE_CURRENT_BINARY_DIR}" "${SPECTRA_ICON}")
        file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/spectra.rc"
             CONTENT "IDI_APP_ICON ICON \"${SPECTRA_ICON_REL}\"\n")
        set(SPECTRA_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/spectra.rc")
    endif()
endif()

# The engine, by layer. Kept as lists rather than one flat add_executable so the
# order of the file below is the order of the architecture document, and a new
# file has one obvious home. The dependency rule is: a layer may only reach the
# layers below it.
set(SPECTRA_SRC_APP
    src/app/main.cpp
    src/app/Application.cpp
    src/app/Launcher.cpp
)
set(SPECTRA_SRC_EDITOR
    src/editor/Editor.cpp
    src/editor/EditorLayout.cpp
    src/editor/PanelLayout.cpp
    src/editor/Picking.cpp
    src/editor/Gizmo.cpp
    src/editor/AssetBrowser.cpp
    src/editor/panels/TopBar.cpp
    src/editor/panels/TopBarOverlay.cpp
    src/editor/panels/LandformPanel.cpp
    src/editor/panels/CloudPanel.cpp
    src/editor/panels/ParticlesPanel.cpp
    src/editor/panels/LeftColumn.cpp
    src/editor/panels/FavoritesDock.cpp
    src/editor/panels/ScenePanel.cpp
    src/editor/panels/InspectorDock.cpp
    src/editor/panels/BottomBar.cpp
    src/editor/panels/SoundsDock.cpp
    src/editor/panels/GuiEditor.cpp
)
set(SPECTRA_SRC_SCRIPT
    src/script/ScriptHost.cpp
    src/script/BindEngine.cpp
    src/script/BindScene.cpp
    src/script/BindCamera.cpp
    src/script/BindInput.cpp
    src/script/BindSound.cpp
    src/script/BindString.cpp
    src/script/BindParticles.cpp
    src/script/BindGui.cpp
)
set(SPECTRA_SRC_SCENE
    src/scene/BodyFactory.cpp
    src/scene/CameraEntity.cpp
    src/scene/Cloud.cpp
    src/scene/Landform.cpp
    src/scene/Noise.cpp
    src/scene/SceneSerializer.cpp
)
set(SPECTRA_SRC_UI
    src/ui/UiContext.cpp
    src/ui/Theme.cpp
)
# The interface of a game: the canvas and the thing that draws it. Between the
# render layer and the ui layer: it needs colours, rectangles, letters and
# pictures from the renderer, and the scene writes it down.
set(SPECTRA_SRC_GUI
    src/gui/GuiCanvas.cpp
    src/gui/GuiRenderer.cpp
)
set(SPECTRA_SRC_RENDER
    src/render/Camera.cpp
    src/render/FontFile.cpp
    src/render/GlyphCache.cpp
    src/render/ParticleSystem.cpp
    src/render/PrimitiveCache.cpp
    src/render/ShaderProgram.cpp
    src/render/ShadowMap.cpp
    src/render/Skybox.cpp
    src/render/TextAtlas.cpp
    src/render/UiRenderer.cpp
    src/render/SceneRenderer.cpp
    src/render/WaterSystem.cpp
)
set(SPECTRA_SRC_ASSETS
    src/assets/MeshImport.cpp
    src/assets/MeshRegistry.cpp
    src/assets/TextureRegistry.cpp
    src/assets/MeshUtil.cpp
    src/assets/loaders/GltfLoader.cpp
    src/assets/loaders/ObjLoader.cpp
    src/assets/loaders/PlyLoader.cpp
    src/assets/loaders/FbxLoader.cpp
)
set(SPECTRA_SRC_AUDIO
    src/audio/SoundDevice.cpp
)
set(SPECTRA_SRC_PHYSICS
    src/physics/RigidBody.cpp
    src/physics/PhysicsWorld.cpp
)
set(SPECTRA_SRC_PLATFORM
    src/platform/Window.cpp
    src/platform/Filesystem.cpp
    src/platform/GlFunctions.cpp
    src/platform/Screenshot.cpp
)

set(SPECTRA_SRC_CORE
    src/core/Log.cpp
    src/core/CrashLog.cpp
    src/core/Project.cpp
)
set(SPECTRA_SRC_I18N
    src/i18n/Localizer.cpp
)

# ---------------------------------------------------------------------------
# spectra_engine - the engine without an editor
# ---------------------------------------------------------------------------
#
# Everything below app/ and editor/: the layers a game needs and nothing that
# draws a dock. The split exists so a second executable can be the same engine
# with an empty window and a loop - which is what "Build" already promises - and
# so the editor and that executable cannot drift apart by accident.
#
# `app/` and `editor/` stay in the executable, which is the honest reading of the
# layer graph: both sit above everything here and both are the shell, not the
# engine. Nothing below them includes either (checked: only Application.hpp does,
# and it is the one that owns both).
#
# The engine is compiled once, into an object library, and is then used twice:
#
#   spectra_runtime  static  - the core inside the program. The editor and
#                               everything it starts is one file, with no core
#                               beside it that can be lost, cannot be found or
#                               be of the wrong version.
#   spectra_player   shared  - the core as a file: SpectraPlayer.dll. This is
#                               what a project carries in its `lib/` folder and
#                               what a built game has beside it, so a project
#                               says which core it was made with.
#
# One compilation, two shapes of the same code: the sources cannot drift apart,
# because there is only one set of them.
add_library(spectra_engine OBJECT
    ${SPECTRA_SRC_SCENE}
    ${SPECTRA_SRC_UI}
    ${SPECTRA_SRC_GUI}
    ${SPECTRA_SRC_RENDER}
    ${SPECTRA_SRC_ASSETS}
    ${SPECTRA_SRC_AUDIO}
    ${SPECTRA_SRC_PHYSICS}
    ${SPECTRA_SRC_PLATFORM}
    ${SPECTRA_SRC_CORE}
    ${SPECTRA_SRC_I18N}
)
if(SPECTRA_SCRIPTING)
    target_sources(spectra_engine PRIVATE ${SPECTRA_SRC_SCRIPT})
endif()

# The objects go into a shared library as well as into the program, so they are
# compiled to be position independent.
set_target_properties(spectra_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Own headers are found by layer name, never by a relative path: a file that moves
# between directories then needs no edit, and there is no "../.." in the tree.
target_include_directories(spectra_engine PUBLIC ${SPECTRA_INCLUDE})

# Compile-time location of the shipped assets, so the engine also runs straight
# out of the source tree. The staged copy next to the binary is what it prefers.
target_compile_definitions(spectra_engine PRIVATE SPECTRA_ASSET_DIR="${SPECTRA_ASSETS}")

# Where the shaders are in the source tree, so that the engine runs out of the
# tree without anything being staged. The staged copy next to the binary is what
# it prefers.
target_compile_definitions(spectra_engine PRIVATE
    SPECTRA_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src")

target_link_libraries(spectra_engine PUBLIC
    spectra_freetype
    spectra_ufbx
    spectra_miniply
    spectra_gltf
    spectra_obj
    spectra_json
    spectra_stb
    spectra_miniaudio
)
if(SPECTRA_SCRIPTING)
    target_compile_definitions(spectra_engine PUBLIC SPECTRA_SCRIPTING=1)
    target_link_libraries(spectra_engine PUBLIC spectra_angelscript)
endif()
spectra_warnings(spectra_engine)
spectra_optimise(spectra_engine)

# The core inside the program. The editor links this, and so does every tool
# that needs the engine without a window.
add_library(spectra_runtime STATIC)
target_link_libraries(spectra_runtime PUBLIC spectra_engine)

# The core as a file. Everything the program uses is exported, so the layer
# headers need no decoration of their own: a class that is used across the
# boundary is exported, and one that is not is not.
add_library(spectra_core SHARED)
target_link_libraries(spectra_core PUBLIC spectra_engine)
spectra_optimise(spectra_core)
spectra_link_optimise(spectra_core)
set_target_properties(spectra_core PROPERTIES
    OUTPUT_NAME SpectraPlayer
    WINDOWS_EXPORT_ALL_SYMBOLS ON)
if(WIN32)
    # No "lib" in front on Windows: the core is "SpectraPlayer.dll", not
    # "libSpectraPlayer.dll". Everywhere else the prefix is the one the platform
    # uses, and it stays.
    set_target_properties(spectra_core PROPERTIES PREFIX "")
endif()
if(MINGW)
    # The same thing for the GNU linker: without it a DLL exports nothing but
    # what is marked, and the program that links it finds nothing to link.
    target_link_options(spectra_core PRIVATE "LINKER:--export-all-symbols")
endif()

# ---------------------------------------------------------------------------
# spectra_game - the game, without a shell around it
# ---------------------------------------------------------------------------
#
# The world, the picture of it, the sound, the scripts and the loop that runs
# them. Both programs are built on it: the editor plays the game through it,
# and the player program is it and nothing else - so the game a player plays
# is the game the editor edits, and the two cannot drift apart.
#
# It is compiled once, into a static library, and linked into both.
add_library(spectra_game STATIC src/app/GameHost.cpp)
target_link_libraries(spectra_game PUBLIC spectra_runtime)
spectra_warnings(spectra_game)
spectra_optimise(spectra_game)

# ---------------------------------------------------------------------------
# spectra3d - the editor
# ---------------------------------------------------------------------------

add_executable(spectra3d
    ${SPECTRA_ICON_RC}
    ${SPECTRA_SRC_APP}
    ${SPECTRA_SRC_EDITOR}
)
target_link_libraries(spectra3d PRIVATE spectra_game)
spectra_warnings(spectra3d)
spectra_optimise(spectra3d)
spectra_link_optimise(spectra3d)

# ---------------------------------------------------------------------------
# spectra_player - the program that plays a game
# ---------------------------------------------------------------------------
#
# The engine, a window and a loop, with no launcher and no editor in it. It
# opens the boot file beside it and plays the game that file names, and it is
# what a build of a game is made of: the editor copies this program into the
# build folder and renames it after the game.
#
# The program carries the core inside it - it links the static library, not the
# shared one - so a build cannot lose its core and cannot be of the wrong
# version.
add_executable(spectra_player
    ${SPECTRA_ICON_RC}
    src/player/Player.cpp
    src/player/main.cpp
)
target_link_libraries(spectra_player PRIVATE spectra_game)
spectra_warnings(spectra_player)
spectra_optimise(spectra_player)
spectra_link_optimise(spectra_player)
set_target_properties(spectra_player PROPERTIES OUTPUT_NAME SpectraPlayer)

# ---------------------------------------------------------------------------
# Platform
# ---------------------------------------------------------------------------
#
# On the engine, not the executable: the window, the GL context and the shell
# folder dialog all live in platform/ and Filesystem.cpp is what wants COM.
#
# On the engine and not on either of its two shapes, so that the program and the
# core-as-a-file are linked the same way.

if(WIN32)
    # windows.h is trimmed per file instead of from here: rapidobj.hpp redefines
    # both macros unconditionally, and a command-line -DNOMINMAX (which CMake
    # writes as -DNOMINMAX="") is a *different* definition, so the build warns
    # about a header that is not ours. Application.cpp, Filesystem.cpp, Window.cpp
    # and ObjLoader.cpp guard the same two defines around their windows.h include.
    #
    # opengl32 for the fixed pipeline, gdi32 + user32 for the window icon and the
    # shell folder dialog's window, ole32/shell32/uuid for that dialog's COM,
    # kernel32 for the waitable timer the frame cap uses, winmm for the 1 ms timer
    # resolution it needs.
    target_link_libraries(spectra_engine PUBLIC opengl32 gdi32 user32 kernel32)
    target_link_libraries(spectra_engine PUBLIC ole32 shell32 uuid winmm)

    # The names of the functions in a crash report.
    #
    # DbgHelp reads the symbols of the program and of the modules it loaded, so
    # a report says "the picture was drawn here" instead of an address nobody
    # can look up. It is optional: without it the report still has the stack,
    # only the names are missing, and a toolchain without DbgHelp is not a
    # reason for the report to be empty.
    find_library(SPECTRA_DBGHELP_LIBRARY NAMES dbghelp)
    if(SPECTRA_DBGHELP_LIBRARY)
        target_link_libraries(spectra_engine PUBLIC ${SPECTRA_DBGHELP_LIBRARY})
        target_compile_definitions(spectra_engine PUBLIC SPECTRA_CRASH_SYMBOLS=1)
        message(STATUS "Crash report symbols: ${SPECTRA_DBGHELP_LIBRARY}")
    endif()

    # GLFW is vendored as a prebuilt for a handful of toolchains. The newest one
    # that exists on disk is used, so a machine with only the 2026 drop still
    # builds without anyone editing this file.
    if(MSVC)
        set(SPECTRA_GLFW_VENDORS vc2026 vc2022 vc2019 vc2017)
        set(SPECTRA_GLFW_NAMES glfw3.lib)
    else()
        set(SPECTRA_GLFW_VENDORS lib-mingw-w64)
        set(SPECTRA_GLFW_NAMES libglfw3.a glfw3.a)
    endif()
    foreach(vendor IN LISTS SPECTRA_GLFW_VENDORS)
        foreach(name IN LISTS SPECTRA_GLFW_NAMES)
            set(candidate "${SPECTRA_THIRD_PARTY}/glfw/${vendor}/${name}")
            if(EXISTS "${candidate}" AND NOT SPECTRA_GLFW_LIB)
                set(SPECTRA_GLFW_LIB "${candidate}")
                set(SPECTRA_GLFW_DLL "${SPECTRA_THIRD_PARTY}/glfw/${vendor}/glfw3.dll")
            endif()
        endforeach()
    endforeach()
    if(NOT SPECTRA_GLFW_LIB)
        message(FATAL_ERROR
            "No prebuilt GLFW found in third_party/glfw (looked for: ${SPECTRA_GLFW_VENDORS})")
    endif()
    target_link_libraries(spectra_engine PUBLIC "${SPECTRA_GLFW_LIB}")
    target_include_directories(spectra_engine SYSTEM PUBLIC third_party/glfw/include)
    message(STATUS "GLFW: ${SPECTRA_GLFW_LIB}")
    # The DLL travels with the binary. A build that is copied to another machine
    # and cannot start is worse than no build. Staged on every build, for the
    # same reason the assets are: a POST_BUILD step only runs when the
    # executable is relinked, and a runtime that has been replaced has to be
    # replaced next to it.
    add_custom_target(spectra_runtime_dlls ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SPECTRA_GLFW_DLL}" "$<TARGET_FILE_DIR:spectra3d>"
        COMMENT "Staging glfw3.dll")

    # The MinGW runtime DLLs, next to the binary for the same reason - and with
    # more force, because a missing one does not always fail here. It fails on the
    # machine that runs it: the loader finds an older libstdc++-6.dll from another
    # MinGW on PATH, binds the executable to it, and then reports a missing
    # _ZSt... symbol in the executable rather than in the library that is wrong.
    # Shipping them means the binary finds its own runtime first, whichever MinGW
    # installations the machine happens to have.
    get_filename_component(SPECTRA_TOOLCHAIN_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    if(EXISTS "${SPECTRA_TOOLCHAIN_BIN}")
        foreach(runtime_lib libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
            if(EXISTS "${SPECTRA_TOOLCHAIN_BIN}/${runtime_lib}")
                add_custom_command(TARGET spectra_runtime_dlls PRE_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${SPECTRA_TOOLCHAIN_BIN}/${runtime_lib}"
                            "$<TARGET_FILE_DIR:spectra3d>"
                    COMMENT "Staging ${runtime_lib}")
            endif()
        endforeach()
    endif()
elseif(APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLFW REQUIRED glfw3)
    target_include_directories(spectra_engine PUBLIC ${GLFW_INCLUDE_DIRS})
    target_link_libraries(spectra_engine PUBLIC ${GLFW_LIBRARIES} ${CMAKE_DL_LIBS})
else()
    find_package(OpenGL REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLFW REQUIRED glfw3)
    # miniaudio talks to ALSA on Linux, and its ALSA backend includes
    # <alsa/asoundlib.h> - so the sound is not optional on this platform
    # without giving the engine a sound that cannot be played at all.
    pkg_check_modules(ALSA REQUIRED alsa)
    target_include_directories(spectra_engine PUBLIC ${GLFW_INCLUDE_DIRS} ${ALSA_INCLUDE_DIRS})
    target_link_libraries(spectra_engine PUBLIC ${GLFW_LIBRARIES} ${ALSA_LIBRARIES} OpenGL::GL ${CMAKE_DL_LIBS})
endif()

# Assets are copied next to the binary so a fresh checkout runs without extra
# setup: languages, models and the images the icon picker lists all live there,
# and the engine also accepts the source tree via SPECTRA_ASSET_DIR.
#
# A target of its own, not a POST_BUILD step on the executable: a POST_BUILD
# command runs when the executable is relinked, and nothing here relinks when
# only an asset changes. The result was a build tree whose assets were a
# version behind the tree they came from - and a sky that had been changed was
# simply not there.
add_custom_target(spectra_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SPECTRA_ASSETS}" "$<TARGET_FILE_DIR:spectra3d>/assets"
    COMMENT "Staging assets")

# The shaders, staged next to the binary like the assets are.
#
# They are staged and not compiled into the program: a shader is a file, and
# a file that is next to the binary can be read, checked and replaced without
# rebuilding the engine. The loader that reads them is the next piece of work;
# until then this only makes sure that the files the engine is going to read
# are the files that are in the tree.
add_custom_target(spectra_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders"
            "$<TARGET_FILE_DIR:spectra3d>/shaders"
    COMMENT "Staging shaders")

# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------

if(SPECTRA_BUILD_TOOLS)
    add_executable(glbinfo tools/glbinfo.cpp)
    target_include_directories(glbinfo PRIVATE third_party/models_lib/cgltf)
    spectra_warnings(glbinfo)

    add_executable(fbxtest tools/fbxtest.cpp)
    target_link_libraries(fbxtest PRIVATE spectra_ufbx)
    spectra_warnings(fbxtest)

    # The generator for the sky the engine ships: assets/sky.jpg is a picture
    # this makes, and the engine loads it like any other texture. It is a tool
    # rather than part of the engine because the picture is made once - the
    # file it writes is the asset, not the tool.
    add_executable(skygen tools/skygen.cpp)
    target_link_libraries(skygen PRIVATE spectra_stb)
    spectra_warnings(skygen)

    # The generator for the test sounds: three short WAV files, two seconds
    # each, made to be told apart by ear.
    add_executable(soundgen tools/soundgen.cpp)
    target_link_libraries(soundgen PRIVATE spectra_stb)
    spectra_warnings(soundgen)

    # A picture of the shadows, without an editor: a ground, three boxes and
    # the settings the shadows are made with, written out as a PNG. The
    # shadows are the one part of the picture that cannot be checked by
    # reading the code.
    add_executable(shadowtest tools/shadowtest.cpp)
    target_link_libraries(shadowtest PRIVATE spectra_runtime spectra_stb)
    spectra_warnings(shadowtest)

    # A picture of the water, without an editor: a ground under a sheet of
    # water and the settings the water is made with, written out as a PNG.
    # The water is the other part of the picture that cannot be checked by
    # reading the code.
    add_executable(watertest tools/watertest.cpp)
    target_link_libraries(watertest PRIVATE spectra_runtime spectra_stb)
    spectra_warnings(watertest)

    # Headless check of the particle system: the pool, the step, the emitters
    # and the names of the effects. The simulation touches no GL - a particle
    # is a point, a colour and a size - so all of it can be checked with no
    # window and no context. What is on the screen is a picture, and a
    # picture is checked by looking at it.
    add_executable(particletest tools/particletest.cpp)
    target_link_libraries(particletest PRIVATE spectra_runtime)
    spectra_warnings(particletest)

    enable_testing()
    add_test(NAME particletest COMMAND particletest)

    if(SPECTRA_SCRIPTING)
        # Headless check of the script host: the entry-point rule, the hooks and
        # their failure modes, with no window and no project.
        #
        # It used to name eighteen engine source files by hand, which meant a file
        # added to the engine was invisible here until something stopped linking -
        # and a source list is not a dependency list. spectra_runtime is both, so
        # this is one line and it cannot go stale.
        add_executable(scripttest tools/scripttest.cpp)
        target_link_libraries(scripttest PRIVATE spectra_runtime)
        spectra_warnings(scripttest)

        # Headless check of the editor's panel geometry, of the picture and the
        # pointer agreeing about where a thing is, and of the font the engine
        # ships. The editor's own sources are compiled in, because the geometry
        # is theirs: a layout that can only be checked by opening the editor and
        # looking at it is a layout that is checked by nobody.
        add_executable(editortest tools/editortest.cpp ${SPECTRA_SRC_EDITOR})
        target_link_libraries(editortest PRIVATE spectra_runtime)
        spectra_warnings(editortest)

        enable_testing()
        add_test(NAME scripttest COMMAND scripttest)
        add_test(NAME editortest COMMAND editortest)
    endif()

    # The tools are built with the same flags as the engine: they link the
    # engine, and the engine's objects are compiled with the link-time
    # optimisation - a tool that links them without it is a tool that cannot
    # be linked.
    foreach(tool glbinfo fbxtest skygen soundgen shadowtest particletest
                 scripttest editortest)
        if(TARGET ${tool})
            spectra_optimise(${tool})
            spectra_link_optimise(${tool})
        endif()
    endforeach()
endif()
# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------

install(TARGETS spectra3d RUNTIME DESTINATION bin)
# The player: the program that plays a game. A build of a game is made of it,
# and it is installed next to the editor so the editor can find it.
install(TARGETS spectra_player RUNTIME DESTINATION bin)
# The core as a file, next to the program that has it inside: a project carries
# a copy of it, and a built game has it beside the program.
install(TARGETS spectra_core RUNTIME DESTINATION bin)
if(SPECTRA_BUILD_TOOLS)
    install(TARGETS glbinfo fbxtest skygen soundgen shadowtest particletest RUNTIME DESTINATION bin)
    if(SPECTRA_SCRIPTING)
        install(TARGETS scripttest editortest RUNTIME DESTINATION bin)
    endif()
endif()
# The engine looks for assets relative to the binary first and share/spectra3d
# second, so an installed copy runs from anywhere.
install(DIRECTORY assets/ DESTINATION share/spectra3d)
# The shaders are files the engine reads, and they live next to the binary.
install(DIRECTORY src/shaders/ DESTINATION bin/shaders)
if(WIN32 AND SPECTRA_GLFW_DLL)
    install(FILES "${SPECTRA_GLFW_DLL}" DESTINATION bin)
endif()
