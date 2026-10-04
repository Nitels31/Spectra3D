cmake_minimum_required(VERSION 3.16)
project(Spectra3D LANGUAGES C CXX)

# Single source of truth for the build. build_windows.bat / build_linux.sh are
# thin wrappers around this file.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} ${CMAKE_BINARY_DIR})
endforeach()

# Developer utilities (glbinfo, fbxtest, scripttest). Off by default so a normal
# build produces exactly one executable - the engine. Turn on with
# -DSPECTRA_BUILD_TOOLS=ON when working on assets or the scripting layer.
option(SPECTRA_BUILD_TOOLS "Build the standalone asset inspection tools" OFF)
option(SPECTRA_SCRIPTING "Build the AngelScript runtime" ON)

set(SPECTRA_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
set(SPECTRA_ASSETS ${CMAKE_CURRENT_SOURCE_DIR}/assets)

# ---------------------------------------------------------------------------
# Third-party static libraries
# ---------------------------------------------------------------------------

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
target_include_directories(spectra_freetype PUBLIC third_party/freetype/include)
target_compile_definitions(spectra_freetype PRIVATE FT2_BUILD_LIBRARY)
if(NOT MSVC)
    target_compile_options(spectra_freetype PRIVATE -w)
endif()

add_library(spectra_ufbx STATIC third_party/ufbx/ufbx.c)
target_include_directories(spectra_ufbx PUBLIC third_party/ufbx)

add_library(spectra_miniply STATIC third_party/miniply/miniply.cpp)
target_include_directories(spectra_miniply PUBLIC third_party/miniply)

# ---------------------------------------------------------------------------
# Scripting (AngelScript 2.38.0)
# ---------------------------------------------------------------------------

if(SPECTRA_SCRIPTING)
    file(GLOB ANGELSCRIPT_SOURCES ${SPECTRA_THIRD_PARTY}/angelscript/source/*.cpp)
    add_library(spectra_angelscript STATIC ${ANGELSCRIPT_SOURCES})
    target_include_directories(spectra_angelscript
        PUBLIC third_party/angelscript/include
        PRIVATE third_party/angelscript/source)
    if(NOT MSVC)
        target_compile_options(spectra_angelscript PRIVATE -w)
    endif()
endif()

# ---------------------------------------------------------------------------
# Engine
# ---------------------------------------------------------------------------

add_executable(spectra3d
    src/app/main.cpp
    src/app/Application.cpp
    src/app/Launcher.cpp
    src/script/ScriptHost.cpp
    src/script/BindEngine.cpp
    src/script/BindScene.cpp
    src/script/BindCamera.cpp
    src/script/BindString.cpp
    src/core/Log.cpp
    src/core/Project.cpp
    src/platform/Window.cpp
    src/platform/Filesystem.cpp
    src/physics/RigidBody.cpp
    src/physics/PhysicsWorld.cpp
    src/assets/MeshImport.cpp
    src/assets/MeshRegistry.cpp
    src/assets/MeshUtil.cpp
    src/assets/loaders/GltfLoader.cpp
    src/assets/loaders/ObjLoader.cpp
    src/assets/loaders/PlyLoader.cpp
    src/assets/loaders/FbxLoader.cpp
    src/render/Camera.cpp
    src/render/PrimitiveCache.cpp
    src/render/TextAtlas.cpp
    src/render/UiRenderer.cpp
    src/render/SceneRenderer.cpp
    src/ui/UiContext.cpp
    src/ui/Theme.cpp
    src/scene/BodyFactory.cpp
    src/scene/SceneSerializer.cpp
    src/i18n/Localizer.cpp
    src/editor/Editor.cpp
    src/editor/Picking.cpp
    src/editor/Gizmo.cpp
    src/editor/AssetBrowser.cpp
    src/editor/panels/TopBar.cpp
    src/editor/panels/TopBarOverlay.cpp
    src/editor/panels/LeftColumn.cpp
    src/editor/panels/ScenePanel.cpp
    src/editor/panels/InspectorDock.cpp
    src/editor/panels/BottomBar.cpp
)
target_include_directories(spectra3d PRIVATE
    src
    third_party/glfw/include
    third_party/freetype/include
    third_party/cgltf
    third_party/rapidobj
    third_party/miniply
    third_party/ufbx
)
target_compile_definitions(spectra3d PRIVATE SPECTRA_ASSET_DIR="${SPECTRA_ASSETS}")
target_link_libraries(spectra3d PRIVATE spectra_freetype spectra_ufbx spectra_miniply)
if(SPECTRA_SCRIPTING)
    target_compile_definitions(spectra3d PRIVATE SPECTRA_SCRIPTING=1)
    target_link_libraries(spectra3d PRIVATE spectra_angelscript)
    target_include_directories(spectra3d PRIVATE third_party/angelscript/include)
endif()

if(MSVC)
    target_compile_options(spectra3d PRIVATE /W3 /permissive-)
else()
    target_compile_options(spectra3d PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

if(WIN32)
    target_compile_definitions(spectra3d PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(spectra3d PRIVATE opengl32 gdi32 user32 kernel32)
    # Native folder dialog: ole32 for COM, shell32 for the shell item factory,
    # uuid for CLSID_FileOpenDialog / IID_IFileDialog. winmm for the 1 ms timer
    # resolution the frame cap needs.
    target_link_libraries(spectra3d PRIVATE ole32 shell32 uuid winmm)
    if(MSVC)
        target_link_libraries(spectra3d PRIVATE
            "${SPECTRA_THIRD_PARTY}/glfw/lib-vc2022/glfw3.lib")
        set(SPECTRA_GFW_DLL "${SPECTRA_THIRD_PARTY}/glfw/lib-vc2022/glfw3.dll")
    else()
        target_link_libraries(spectra3d PRIVATE
            "${SPECTRA_THIRD_PARTY}/glfw/lib-mingw-w64/libglfw3.a")
        set(SPECTRA_GFW_DLL "${SPECTRA_THIRD_PARTY}/glfw/lib-mingw-w64/glfw3.dll")
    endif()
    add_custom_command(TARGET spectra3d POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SPECTRA_GFW_DLL}" "$<TARGET_FILE_DIR:spectra3d>")
elseif(APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLFW REQUIRED glfw3)
    target_include_directories(spectra3d PRIVATE ${GLFW_INCLUDE_DIRS})
    target_link_libraries(spectra3d PRIVATE ${GLFW_LIBRARIES} ${CMAKE_DL_LIBS})
else()
    find_package(OpenGL REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLFW REQUIRED glfw3)
    target_include_directories(spectra3d PRIVATE ${GLFW_INCLUDE_DIRS})
    target_link_libraries(spectra3d PRIVATE ${GLFW_LIBRARIES} OpenGL::GL ${CMAKE_DL_LIBS})
endif()

# Assets are copied next to the binary so a fresh checkout runs without extra
# setup; the engine also accepts the source tree via SPECTRA_ASSET_DIR.
add_custom_command(TARGET spectra3d POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${SPECTRA_ASSETS}" "$<TARGET_FILE_DIR:spectra3d>/assets"
    COMMENT "Staging assets")

# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------

if(SPECTRA_BUILD_TOOLS)
    add_executable(glbinfo tools/glbinfo.cpp)
    target_include_directories(glbinfo PRIVATE third_party/cgltf)

    add_executable(fbxtest tools/fbxtest.cpp)
    target_include_directories(fbxtest PRIVATE third_party/ufbx)
    target_link_libraries(fbxtest PRIVATE spectra_ufbx)

    if(SPECTRA_SCRIPTING)
        # Headless check of the script host: the entry-point rule and the optional
        # hooks, with no window and no project. It links the script layer plus the
        # world, registry and camera the bindings mutate, which is why the asset
        # loaders come along - MeshRegistry::importFile is one of them.
add_executable(scripttest
tools/scripttest.cpp
            src/script/ScriptHost.cpp
            src/script/BindEngine.cpp
            src/script/BindScene.cpp
            src/script/BindCamera.cpp
            src/script/BindString.cpp
            src/scene/BodyFactory.cpp
            src/core/Log.cpp
            src/physics/PhysicsWorld.cpp
            src/physics/RigidBody.cpp
            src/assets/MeshRegistry.cpp
            src/assets/MeshImport.cpp
            src/assets/MeshUtil.cpp
            src/assets/loaders/FbxLoader.cpp
            src/assets/loaders/GltfLoader.cpp
            src/assets/loaders/ObjLoader.cpp
            src/assets/loaders/PlyLoader.cpp
            src/render/Camera.cpp
        )
        target_include_directories(scripttest PRIVATE
            src
            third_party/cgltf
            third_party/rapidobj
            third_party/miniply
            third_party/ufbx
            third_party/angelscript/include
        )
        target_compile_definitions(scripttest PRIVATE SPECTRA_SCRIPTING=1)
        target_link_libraries(scripttest PRIVATE spectra_angelscript spectra_ufbx spectra_miniply)
        if(MSVC)
            target_compile_options(scripttest PRIVATE /W3 /permissive-)
        else()
            target_compile_options(scripttest PRIVATE -Wall -Wextra -Wno-unused-parameter)
        endif()
    endif()
endif()

install(TARGETS spectra3d RUNTIME DESTINATION bin)
install(DIRECTORY assets/ DESTINATION share/spectra3d)
