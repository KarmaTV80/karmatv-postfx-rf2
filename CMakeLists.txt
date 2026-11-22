cmake_minimum_required(VERSION 3.24)

project(KarmaTV_PostFX VERSION 1.1.0 LANGUAGES CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "KarmaTV PostFX si compila solo a 64 bit (rFactor 2 e' x64).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Runtime statico: l'add-on non richiede i redistributable di Visual C++.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# --- Dipendenze ---------------------------------------------------------------
# Header dell'API add-on di ReShade e la versione di Dear ImGui che quella release
# si aspetta (reshade_overlay.hpp controlla IMGUI_VERSION_NUM).
# La versione di ReShade installata in rF2 deve essere uguale o piu' recente.
include(FetchContent)
set(KTV_RESHADE_TAG "v6.8.0" CACHE STRING "Tag di ReShade da cui prendere gli header add-on")

FetchContent_Declare(reshade
    GIT_REPOSITORY https://github.com/crosire/reshade.git
    GIT_TAG        ${KTV_RESHADE_TAG}
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES deps/imgui
    # Nessun CMakeLists in questa sottocartella: vengono solo scaricati i sorgenti.
    SOURCE_SUBDIR  include
)
FetchContent_MakeAvailable(reshade)

# --- Add-on -------------------------------------------------------------------
set(KTV_SOURCES
    src/addon_main.cpp
    src/broadcast/spout_sender.cpp
    src/app.cpp
    src/core/effects_catalog.cpp
    src/core/i18n.cpp
    src/core/log.cpp
    src/core/settings.cpp
    src/input/hotkeys.cpp
    src/input/wheel.cpp
    src/perf/gpu_profiler.cpp
    src/perf/spike_monitor.cpp
    src/render/depth_capture.cpp
    src/render/frame_hooks.cpp
    src/render/mirror_grade.cpp
    src/render/pipeline.cpp
    src/rules/auto_rules.cpp
    src/telemetry/rf2_telemetry.cpp
    src/ui/logo.cpp
    src/update/updater.cpp
    src/ui/overlay.cpp
    src/ui/theme.cpp
    src/resources.rc
)

add_library(KarmaTV_PostFX SHARED ${KTV_SOURCES})

set_target_properties(KarmaTV_PostFX PROPERTIES
    PREFIX ""
    SUFFIX ".addon64"
)

target_include_directories(KarmaTV_PostFX PRIVATE
    src
    ${reshade_SOURCE_DIR}/include
    ${reshade_SOURCE_DIR}/deps/imgui
)

target_compile_definitions(KarmaTV_PostFX PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
    ImTextureID=ImU64
    KTV_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(KarmaTV_PostFX PRIVATE dinput8 dxguid ole32 windowscodecs winhttp bcrypt shell32)

# Strumento per le LUT (preset integrati, LUT da screenshot, anteprime)
add_executable(ktv_lut tools/lut_maker/main.cpp)
target_link_libraries(ktv_lut PRIVATE windowscodecs ole32)
target_compile_definitions(ktv_lut PRIVATE UNICODE _UNICODE)
if(MSVC)
    target_compile_options(ktv_lut PRIVATE /W4 /permissive- /utf-8)
endif()

if(MSVC)
    target_compile_options(KarmaTV_PostFX PRIVATE /W4 /permissive- /utf-8)
endif()
