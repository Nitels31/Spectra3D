cmake_minimum_required(VERSION 3.16)

# include/core/Project.hpp is the single source of truth for both: the version the
# launcher, the manifest and the title bar report, and the folder the application
# icons live in. Reading them here means the build can never disagree with the
# engine about either.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/include/core/Project.hpp" SPECTRA_PROJECT_HEADER
     LIMIT 4096)
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
set(SPECTRA_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/${SPECTRA_ICON_DIR}/icon.ico")

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
option(SPECTRA_BUILD_TOOLS "Build the standalone asset inspection tools" OFF)
option(SPECTRA_SCRIPTING "Build the AngelScript runtime" ON)
option(SPECTRA_WERROR "Treat compiler warnings as errors" OFF)

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
endif()

# Model loaders: ufbx for FBX, miniply for PLY, cgltf and rapidobj straight from
# their headers where a target would carry no compiled code.
add_library(spectra_ufbx STATIC third_party/ufbx/ufbx.c)
target_include_directories(spectra_ufbx SYSTEM PUBLIC third_party/ufbx)

add_library(spectra_miniply STATIC third_party/miniply/miniply.cpp)
target_include_directories(spectra_miniply SYSTEM PUBLIC third_party/miniply)

# Header-only dependencies get an INTERFACE target rather than a bare -I. There is
# nothing to compile, only a directory to add, but the name keeps the include in
# the same shape as every other dependency's.
add_library(spectra_gltf INTERFACE)
target_include_directories(spectra_gltf SYSTEM INTERFACE third_party/cgltf)

add_library(spectra_obj INTERFACE)
target_include_directories(spectra_obj SYSTEM INTERFACE third_party/rapidobj)

# nlohmann/json, vendored as a single header. Reached as "json/json.hpp", the same
# path every other dependency has.
add_library(spectra_json INTERFACE)
target_include_directories(spectra_json SYSTEM INTERFACE ${SPECTRA_INCLUDE})

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
        file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/spectra.rc"
             CONTENT "IDI_APP_ICON ICON \"${SPECTRA_ICON}\"\n")
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
    src/editor/Picking.cpp
    src/editor/Gizmo.cpp
    src/editor/AssetBrowser.cpp
    src/editor/panels/TopBar.cpp
    src/editor/panels/TopBarOverlay.cpp
    src/editor/panels/LeftColumn.cpp
    src/editor/panels/FavoritesDock.cpp
    src/editor/panels/ScenePanel.cpp
    src/editor/panels/InspectorDock.cpp
    src/editor/panels/BottomBar.cpp
)
set(SPECTRA_SRC_SCRIPT
    src/script/ScriptHost.cpp
    src/script/BindEngine.cpp
    src/script/BindScene.cpp
    src/script/BindCamera.cpp
    src/script/BindInput.cpp
    src/script/BindString.cpp
)
set(SPECTRA_SRC_SCENE
    src/scene/BodyFactory.cpp
    src/scene/SceneSerializer.cpp
)
set(SPECTRA_SRC_UI
    src/ui/UiContext.cpp
    src/ui/Theme.cpp
)
set(SPECTRA_SRC_RENDER
    src/render/Camera.cpp
    src/render/PrimitiveCache.cpp
    src/render/TextAtlas.cpp
    src/render/UiRenderer.cpp
    src/render/SceneRenderer.cpp
)
set(SPECTRA_SRC_ASSETS
    src/assets/MeshImport.cpp
    src/assets/MeshRegistry.cpp
    src/assets/MeshUtil.cpp
    src/assets/loaders/GltfLoader.cpp
    src/assets/loaders/ObjLoader.cpp
    src/assets/loaders/PlyLoader.cpp
    src/assets/loaders/FbxLoader.cpp
)
set(SPECTRA_SRC_PHYSICS
    src/physics/RigidBody.cpp
    src/physics/PhysicsWorld.cpp
)
set(SPECTRA_SRC_PLATFORM
    src/platform/Window.cpp
    src/platform/Filesystem.cpp
)
set(SPECTRA_SRC_CORE
    src/core/Log.cpp
    src/core/Project.cpp
)
set(SPECTRA_SRC_I18N
    src/i18n/Localizer.cpp
)

# ---------------------------------------------------------------------------
# spectra_runtime - the engine without an editor
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
add_library(spectra_runtime STATIC
    ${SPECTRA_SRC_SCENE}
    ${SPECTRA_SRC_UI}
    ${SPECTRA_SRC_RENDER}
    ${SPECTRA_SRC_ASSETS}
    ${SPECTRA_SRC_PHYSICS}
    ${SPECTRA_SRC_PLATFORM}
    ${SPECTRA_SRC_CORE}
    ${SPECTRA_SRC_I18N}
)
if(SPECTRA_SCRIPTING)
    target_sources(spectra_runtime PRIVATE ${SPECTRA_SRC_SCRIPT})
endif()

# Own headers are found by layer name, never by a relative path: a file that moves
# between directories then needs no edit, and there is no "../.." in the tree.
target_include_directories(spectra_runtime PUBLIC ${SPECTRA_INCLUDE})

# Compile-time location of the shipped assets, so the engine also runs straight
# out of the source tree. The staged copy next to the binary is what it prefers.
target_compile_definitions(spectra_runtime PRIVATE SPECTRA_ASSET_DIR="${SPECTRA_ASSETS}")

target_link_libraries(spectra_runtime PUBLIC
    spectra_freetype
    spectra_ufbx
    spectra_miniply
    spectra_gltf
    spectra_obj
    spectra_json
)
if(SPECTRA_SCRIPTING)
    target_compile_definitions(spectra_runtime PUBLIC SPECTRA_SCRIPTING=1)
    target_link_libraries(spectra_runtime PUBLIC spectra_angelscript)
endif()
spectra_warnings(spectra_runtime)

# ---------------------------------------------------------------------------
# spectra3d - the editor
# ---------------------------------------------------------------------------

add_executable(spectra3d
    ${SPECTRA_ICON_RC}
    ${SPECTRA_SRC_APP}
    ${SPECTRA_SRC_EDITOR}
)
target_link_libraries(spectra3d PRIVATE spectra_runtime)
spectra_warnings(spectra3d)

# ---------------------------------------------------------------------------
# Platform
# ---------------------------------------------------------------------------
#
# On the runtime, not the executable: the window, the GL context and the shell
# folder dialog all live in platform/ and Filesystem.cpp is what wants COM.

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
    target_link_libraries(spectra_runtime PUBLIC opengl32 gdi32 user32 kernel32)
    target_link_libraries(spectra_runtime PUBLIC ole32 shell32 uuid winmm)

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
    target_link_libraries(spectra_runtime PUBLIC "${SPECTRA_GLFW_LIB}")
    target_include_directories(spectra_runtime SYSTEM PUBLIC third_party/glfw/include)
    message(STATUS "GLFW: ${SPECTRA_GLFW_LIB}")
    # The DLL travels with the binary. A build that is copied to another machine
    # and cannot start is worse than no build.
    add_custom_command(TARGET spectra3d POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SPECTRA_GLFW_DLL}" "$<TARGET_FILE_DIR:spectra3d>"
        COMMENT "Staging glfw3.dll")
elseif(APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLFW REQUIRED glfw3)
    target_include_directories(spectra_runtime PUBLIC ${GLFW_INCLUDE_DIRS})
    target_link_libraries(spectra_runtime PUBLIC ${GLFW_LIBRARIES} ${CMAKE_DL_LIBS})
else()
    find_package(OpenGL REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLFW REQUIRED glfw3)
    target_include_directories(spectra_runtime PUBLIC ${GLFW_INCLUDE_DIRS})
    target_link_libraries(spectra_runtime PUBLIC ${GLFW_LIBRARIES} OpenGL::GL ${CMAKE_DL_LIBS})
endif()

# Assets are copied next to the binary so a fresh checkout runs without extra
# setup: languages, models and the images the icon picker lists all live there,
# and the engine also accepts the source tree via SPECTRA_ASSET_DIR.
add_custom_command(TARGET spectra3d POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SPECTRA_ASSETS}" "$<TARGET_FILE_DIR:spectra3d>/assets"
    COMMENT "Staging assets")

# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------

if(SPECTRA_BUILD_TOOLS)
    add_executable(glbinfo tools/glbinfo.cpp)
    target_include_directories(glbinfo PRIVATE third_party/cgltf)
    spectra_warnings(glbinfo)

    add_executable(fbxtest tools/fbxtest.cpp)
    target_link_libraries(fbxtest PRIVATE spectra_ufbx)
    spectra_warnings(fbxtest)

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

        enable_testing()
        add_test(NAME scripttest COMMAND scripttest)
    endif()
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------

install(TARGETS spectra3d RUNTIME DESTINATION bin)
if(SPECTRA_BUILD_TOOLS)
    install(TARGETS glbinfo fbxtest RUNTIME DESTINATION bin)
    if(SPECTRA_SCRIPTING)
        install(TARGETS scripttest RUNTIME DESTINATION bin)
    endif()
endif()
# The engine looks for assets relative to the binary first and share/spectra3d
# second, so an installed copy runs from anywhere.
install(DIRECTORY assets/ DESTINATION share/spectra3d)
if(WIN32 AND SPECTRA_GLFW_DLL)
    install(FILES "${SPECTRA_GLFW_DLL}" DESTINATION bin)
endif()
