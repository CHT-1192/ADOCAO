cmake_minimum_required(VERSION 3.20)

# Prefer GCC over Clang (both over MSVC). Must be set before project().
if(NOT CMAKE_C_COMPILER AND NOT CMAKE_CXX_COMPILER)
    find_program(GCC_C   gcc)
    find_program(GCC_CXX g++)
    find_program(CLANG_C   clang)
    find_program(CLANG_CXX clang++)
    if(GCC_C AND GCC_CXX)
        set(CMAKE_C_COMPILER   "${GCC_C}"   CACHE FILEPATH "C compiler")
        set(CMAKE_CXX_COMPILER "${GCC_CXX}" CACHE FILEPATH "C++ compiler")
    elseif(CLANG_C AND CLANG_CXX)
        set(CMAKE_C_COMPILER   "${CLANG_C}"   CACHE FILEPATH "C compiler")
        set(CMAKE_CXX_COMPILER "${CLANG_CXX}" CACHE FILEPATH "C++ compiler")
    endif()
endif()

project(ADOCAO VERSION 6.0.0 LANGUAGES CXX C)

option(ADOCAO_PORTABLE "Build a static-linked portable executable" OFF)

# ── Zoom level (min zoom-out distance) ─────────────────────────
set(ADOCAO_ZOOM_LEVEL "Ultra" CACHE STRING
    "Min zoom-out level: Normal(10) Extra(5) Super(2.5) Ultra(1) Hyper(0.5) Extreme(0.25) Unimaginable(0.1)")
set_property(CACHE ADOCAO_ZOOM_LEVEL PROPERTY STRINGS
    "Normal" "Extra" "Super" "Ultra" "Hyper" "Extreme" "Unimaginable")

if(ADOCAO_ZOOM_LEVEL STREQUAL "Normal")
    set(ADOCAO_MIN_ZOOM 10.0)
elseif(ADOCAO_ZOOM_LEVEL STREQUAL "Extra")
    set(ADOCAO_MIN_ZOOM 5.0)
elseif(ADOCAO_ZOOM_LEVEL STREQUAL "Super")
    set(ADOCAO_MIN_ZOOM 2.5)
elseif(ADOCAO_ZOOM_LEVEL STREQUAL "Ultra")
    set(ADOCAO_MIN_ZOOM 1.0)
elseif(ADOCAO_ZOOM_LEVEL STREQUAL "Hyper")
    set(ADOCAO_MIN_ZOOM 0.5)
elseif(ADOCAO_ZOOM_LEVEL STREQUAL "Extreme")
    set(ADOCAO_MIN_ZOOM 0.25)
elseif(ADOCAO_ZOOM_LEVEL STREQUAL "Unimaginable")
    set(ADOCAO_MIN_ZOOM 0.1)
else()
    message(WARNING "Unknown ADOCAO_ZOOM_LEVEL '${ADOCAO_ZOOM_LEVEL}', falling back to Ultra (1.0)")
    set(ADOCAO_MIN_ZOOM 1.0)
endif()
add_compile_definitions(ADOCAO_MIN_ZOOM=${ADOCAO_MIN_ZOOM}f)
message(STATUS "Zoom level: ${ADOCAO_ZOOM_LEVEL} (min ${ADOCAO_MIN_ZOOM}x)")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Maximum safe optimization (no -ffast-math — timing precision is critical)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -march=native -fomit-frame-pointer)
endif()

# ---- Dependencies via FetchContent ----
include(FetchContent)

# rapidjson & stb are header-only with no usable CMake build (rapidjson's
# bundled CMake adds nested projects that fail as a subdirectory). Keep
# FetchContent_Populate for them; silence CMP0169 deprecation warning.
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

# GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# glm
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glm)

# RapidJSON (header-only, fast JSON parsing)
FetchContent_Declare(
    rapidjson
    GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(rapidjson)
if(NOT rapidjson_POPULATED)
    FetchContent_Populate(rapidjson)
endif()

# Dear ImGui
set(IMGUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

# stb (header-only, just need the source for stb_vorbis)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
endif()

# miniaudio (single-header)
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.22
    GIT_SHALLOW TRUE
)
# Suppress miniaudio optional dependency not-found messages
set(MINIAUDIO_NO_EXTRA_NODES ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_LIBVORBIS ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_LIBOPUS ON CACHE BOOL "" FORCE)
set(MINIAUDIO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SDL2_LIBRARY "SDL2_LIBRARY-NOTFOUND" CACHE FILEPATH "" FORCE)
set(STEAMAUDIO_LIBRARY "STEAMAUDIO_LIBRARY-NOTFOUND" CACHE FILEPATH "" FORCE)
FetchContent_MakeAvailable(miniaudio)

# miniz (single-file zip library, public domain)
FetchContent_Declare(
    miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG 3.0.2
    GIT_SHALLOW TRUE
)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(miniz)
target_include_directories(miniz PUBLIC ${miniz_SOURCE_DIR})

# tinyfiledialogs (single-header C library)
FetchContent_Declare(
    tinyfiledialogs
    GIT_REPOSITORY https://github.com/native-toolkit/tinyfiledialogs.git
    GIT_TAG 2.9.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tinyfiledialogs)

# ---- Source files ----
set(SOURCES
    # Application layer
    app/main.cpp
    app/Application.hpp
    app/Application.cpp
    app/LauncherWindow.hpp
    app/LauncherWindow.cpp
    app/LoadingWindow.hpp
    app/LoadingWindow.cpp
    app/LevelLoader.hpp
    app/LevelLoader.cpp
    app/GameWindow.hpp
    app/GameWindow.cpp

    # OpenGL loader
    glad/gl_core.hpp
    glad/gl_core.cpp

    # Level parser
    level/LevelData.hpp
    level/LevelData.cpp
    level/JsonCleaner.hpp
    level/JsonCleaner.cpp

    # Easing
    util/Easing.hpp

    # Logger
    util/Logger.hpp
    util/Logger.cpp

    # Thread pool
    util/ThreadPool.hpp
    util/ThreadPool.cpp

    # Data file reader (zip/folder/fallback)
    util/DataFile.hpp
    util/DataFile.cpp

    # Render
    render/CullSIMD.hpp
    render/Shader.hpp
    render/Shader.cpp
    render/Shaders.hpp
    render/PlanetTrail.hpp
    render/PlanetTrail.cpp

    # Camera
    camera/Camera.hpp
    camera/Camera.cpp

    # Track
    track/TileMesh.hpp
    track/TileMesh.cpp
    track/TileGeometry.hpp
    track/TileGeometry.cpp

    # Game
    game/Planet.hpp
    game/Planet.cpp
    game/PlaybackEngine.hpp
    game/PlaybackEngine.cpp

    # Audio
    audio/AudioEngine.hpp
    audio/AudioEngine.cpp
    audio/HitsoundManager.hpp
    audio/HitsoundManager.cpp
)

# ImGui sources
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

# stb_vorbis (implementation in audio/stb_vorbis_impl.cpp)
add_library(stb_vorbis STATIC audio/stb_vorbis_impl.cpp)
target_include_directories(stb_vorbis PUBLIC ${stb_SOURCE_DIR})

# tinyfiledialogs
add_library(tinyfiledialogs STATIC ${tinyfiledialogs_SOURCE_DIR}/tinyfiledialogs.c)
target_include_directories(tinyfiledialogs PUBLIC ${tinyfiledialogs_SOURCE_DIR})

# ---- Main executable ----
add_executable(adocao ${SOURCES} ${IMGUI_SOURCES})
if(WIN32)
    set_target_properties(adocao PROPERTIES WIN32_EXECUTABLE TRUE OUTPUT_NAME "ADOCAO-Windows")
elseif(APPLE)
    set_target_properties(adocao PROPERTIES OUTPUT_NAME "ADOCAO-macOS")
else()
    set_target_properties(adocao PROPERTIES OUTPUT_NAME "ADOCAO-Linux")
endif()

target_include_directories(adocao PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${miniaudio_SOURCE_DIR}
    ${rapidjson_SOURCE_DIR}/include
)

target_link_libraries(adocao PRIVATE
    glfw
    glm::glm
    stb_vorbis
    tinyfiledialogs
    miniz
)

# Platform-specific
if(WIN32)
    target_link_libraries(adocao PRIVATE opengl32 ole32 shell32)
    target_compile_definitions(adocao PRIVATE NOMINMAX UNICODE _UNICODE)
elseif(UNIX AND NOT APPLE)
    find_package(OpenGL REQUIRED)
    target_link_libraries(adocao PRIVATE OpenGL::GL ${CMAKE_DL_LIBS})
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(WAYLAND wayland-client wayland-egl IMPORTED_TARGET QUIET)
    endif()
    if(WAYLAND_FOUND)
        message(STATUS "Wayland found — GLFW will support Wayland")
    endif()
elseif(APPLE)
    # macOS caps OpenGL at 4.1 core — the renderer only needs 3.3.
    # GLFW propagates Cocoa/IOKit/CoreFoundation; add the rest.
    target_compile_definitions(adocao PRIVATE GL_SILENCE_DEPRECATION)
    target_link_libraries(adocao PRIVATE
        "-framework OpenGL"
        "-framework AudioToolbox"
        "-framework CoreAudio"
        "-framework CoreFoundation"
    )
endif()

# Strip Release binaries
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    if(MINGW)
        target_link_options(adocao PRIVATE -Wl,-s)
    elseif(MSVC)
        target_link_options(adocao PRIVATE /OPT:REF /OPT:ICF)
    endif()
endif()

# Static linking for portable builds
if(ADOCAO_PORTABLE)
    if(MINGW)
        target_link_options(adocao PRIVATE -static)
    elseif(MSVC)
        set_property(TARGET adocao PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

# ---- Install ----
install(TARGETS adocao RUNTIME DESTINATION bin)

# ---- Copy hitsounds, shaders, and MinGW DLLs to build directory ----
add_custom_command(TARGET adocao POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/hitsounds"
    "${CMAKE_BINARY_DIR}/hitsounds"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/shaders"
    "${CMAKE_BINARY_DIR}/shaders"
    COMMENT "Copying hitsounds and shaders to build directory"
)

# Copy MinGW runtime DLLs next to EXE
if(MINGW)
    get_filename_component(MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(MINGW_DLLS
        "${MINGW_BIN}/libgcc_s_seh-1.dll"
        "${MINGW_BIN}/libstdc++-6.dll"
        "${MINGW_BIN}/libwinpthread-1.dll"
    )
    if(NOT EXISTS "${MINGW_BIN}/libgcc_s_seh-1.dll")
        set(MINGW_DLLS
            "${MINGW_BIN}/libgcc_s_dw2-1.dll"
            "${MINGW_BIN}/libstdc++-6.dll"
            "${MINGW_BIN}/libwinpthread-1.dll"
        )
    endif()
    add_custom_command(TARGET adocao POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MINGW_DLLS} "${CMAKE_BINARY_DIR}"
        COMMENT "Copying MinGW runtime DLLs to build directory"
    )
endif()

# ---- Copy example level (if available) ----
if(EXISTS "${CMAKE_SOURCE_DIR}/../re_adojas/lib/example/level.adofai")
    file(COPY ${CMAKE_SOURCE_DIR}/../re_adojas/lib/example/level.adofai
         DESTINATION ${CMAKE_BINARY_DIR})
endif()
