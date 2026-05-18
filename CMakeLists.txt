cmake_minimum_required(VERSION 3.20)
project(ADOCAO VERSION 0.5.0 LANGUAGES CXX C)

option(ADOCAO_PORTABLE "Build a static-linked portable executable" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Maximum safe optimization (no -ffast-math — timing precision is critical)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -march=native -fomit-frame-pointer)
endif()

# ---- Dependencies via FetchContent ----
include(FetchContent)

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

# nlohmann/json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# Dear ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

# stb_image (single-header)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(stb)

# miniaudio (single-header)
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.22
    GIT_SHALLOW TRUE
)
# Suppress miniaudio optional dependency not-found messages
set(MINIAUDIO_NO_LIBVORBIS ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_LIBOPUS ON CACHE BOOL "" FORCE)
set(MINIAUDIO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SDL2_LIBRARY "SDL2_LIBRARY-NOTFOUND" CACHE FILEPATH "" FORCE)
set(STEAMAUDIO_LIBRARY "STEAMAUDIO_LIBRARY-NOTFOUND" CACHE FILEPATH "" FORCE)
FetchContent_MakeAvailable(miniaudio)

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
    app/Application.h
    app/Application.cpp
    app/LauncherWindow.h
    app/LauncherWindow.cpp
    app/LoadingWindow.h
    app/LoadingWindow.cpp
    app/LevelLoader.h
    app/LevelLoader.cpp
    app/GameWindow.h
    app/GameWindow.cpp

    # OpenGL loader
    glad/gl_core.h
    glad/gl_core.cpp

    # Level parser
    level/LevelData.h
    level/LevelData.cpp
    level/JsonCleaner.h
    level/JsonCleaner.cpp

    # Easing
    util/Easing.h

    # Logger
    util/Logger.h
    util/Logger.cpp

    # Render
    render/Shader.h
    render/Shader.cpp
    render/Shaders.h
    render/PlanetTrail.h
    render/PlanetTrail.cpp

    # Camera
    camera/Camera.h
    camera/Camera.cpp

    # Track
    track/TileMesh.h
    track/TileMesh.cpp
    track/TileGeometry.h
    track/TileGeometry.cpp

    # Game
    game/Planet.h
    game/Planet.cpp
    game/PlaybackEngine.h
    game/PlaybackEngine.cpp

    # Audio
    audio/AudioEngine.h
    audio/AudioEngine.cpp
    audio/HitsoundManager.h
    audio/HitsoundManager.cpp
    audio/stb_vorbis_impl.cpp
)

# ImGui sources
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

# stb_image (header-only, implementation in util/stb_impl.cpp)
add_library(stb_image STATIC util/stb_impl.cpp)
target_include_directories(stb_image PUBLIC ${stb_SOURCE_DIR})

# tinyfiledialogs
add_library(tinyfiledialogs STATIC ${tinyfiledialogs_SOURCE_DIR}/tinyfiledialogs.c)
target_include_directories(tinyfiledialogs PUBLIC ${tinyfiledialogs_SOURCE_DIR})

# ---- Main executable ----
add_executable(adocao ${SOURCES} ${IMGUI_SOURCES})
set_target_properties(adocao PROPERTIES WIN32_EXECUTABLE TRUE OUTPUT_NAME "ADOCAO")

target_include_directories(adocao PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${miniaudio_SOURCE_DIR}
)

target_link_libraries(adocao PRIVATE
    glfw
    glm::glm
    nlohmann_json::nlohmann_json
    stb_image
    tinyfiledialogs
)

# Platform-specific
if(WIN32)
    target_link_libraries(adocao PRIVATE opengl32 ole32)
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
endif()

# Static linking for portable builds
if(ADOCAO_PORTABLE)
    if(MINGW)
        target_link_options(adocao PRIVATE -static -static-libgcc -static-libstdc++)
    elseif(MSVC)
        set_property(TARGET adocao PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    set_target_properties(adocao PROPERTIES OUTPUT_NAME "ADOCAO-Portable")
endif()

# ---- Install ----
install(TARGETS adocao RUNTIME DESTINATION bin)

# ---- Copy assets to build directory ----
add_custom_command(TARGET adocao POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/assets"
    "${CMAKE_BINARY_DIR}/assets"
    COMMENT "Copying assets to build directory"
)

# ---- Copy example level (if available) ----
if(EXISTS "${CMAKE_SOURCE_DIR}/../re_adojas/lib/example/level.adofai")
    file(COPY ${CMAKE_SOURCE_DIR}/../re_adojas/lib/example/level.adofai
         DESTINATION ${CMAKE_BINARY_DIR})
endif()
