cmake_minimum_required(VERSION 3.24)

project(UniversalFnaf
        VERSION 1.0.0
        DESCRIPTION "Non-invasive outbound traffic filter with a DWM/DirectX 11 overlay"
        LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "UniversalFnaf is Windows-only (WFP + DWM + DirectX 11).")
endif()

# ---------------------------------------------------------------------------
# Global build settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT: the process ships a single self-contained .exe, and no SxS
# assembly reference is required in the embedded manifest (see
# UNIVERSALFNAF_MANIFEST_VIA_RC below).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(STATUS "UniversalFnaf: configuring a 32-bit (x86) build. "
                   "For the intended configuration use the x64 Native Tools prompt "
                   "(cmake --preset msvc-x64-release).")
else()
    message(STATUS "UniversalFnaf: configuring a 64-bit (x64/ARM64) build.")
endif()

option(UNIVERSALFNAF_MANIFEST_VIA_RC
       "Embed resources/app.manifest through the .rc file and disable the linker-generated manifest"
       ON)

# ---------------------------------------------------------------------------
# Third-party dependencies
#   Dear ImGui  : UI rendering (win32 + DirectX 11 backends)
#   nlohmann/json: config.json serialization (header-only)
#
# Both can be satisfied from a local checkout (offline builds):
#   cmake -DUNIVERSALFNAF_IMGUI_DIR=C:/deps/imgui -DUNIVERSALFNAF_JSON_DIR=C:/deps/json ..
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

set(UNIVERSALFNAF_IMGUI_TAG "v1.90.9"  CACHE STRING "Dear ImGui git tag")
set(UNIVERSALFNAF_JSON_TAG  "v3.11.3"  CACHE STRING "nlohmann/json git tag")
set(UNIVERSALFNAF_IMGUI_DIR ""         CACHE PATH   "Local Dear ImGui checkout (contains imgui.cpp)")
set(UNIVERSALFNAF_JSON_DIR  ""         CACHE PATH   "Local nlohmann/json checkout (contains single_include/)")

if(UNIVERSALFNAF_IMGUI_DIR)
    if(NOT EXISTS "${UNIVERSALFNAF_IMGUI_DIR}/imgui.cpp")
        message(FATAL_ERROR "UNIVERSALFNAF_IMGUI_DIR='${UNIVERSALFNAF_IMGUI_DIR}' does not contain imgui.cpp")
    endif()
    set(UNIVERSALFNAF_IMGUI_ROOT "${UNIVERSALFNAF_IMGUI_DIR}")
else()
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        ${UNIVERSALFNAF_IMGUI_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(imgui)   # imgui has no CMakeLists: content is only populated
    set(UNIVERSALFNAF_IMGUI_ROOT "${imgui_SOURCE_DIR}")
endif()

if(UNIVERSALFNAF_JSON_DIR)
    add_library(universalfnaf_json INTERFACE)
    target_include_directories(universalfnaf_json INTERFACE "${UNIVERSALFNAF_JSON_DIR}/single_include")
    set(UNIVERSALFNAF_JSON_TARGET universalfnaf_json)
else()
    FetchContent_Declare(json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        ${UNIVERSALFNAF_JSON_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(json)
    set(UNIVERSALFNAF_JSON_TARGET nlohmann_json::nlohmann_json)
endif()

# ---------------------------------------------------------------------------
# Dear ImGui is compiled as its own target: the project keeps /W4 /permissive-,
# while third-party code is built with the default warning level.
# ---------------------------------------------------------------------------
add_library(universalfnaf_imgui STATIC
    "${UNIVERSALFNAF_IMGUI_ROOT}/imgui.cpp"
    "${UNIVERSALFNAF_IMGUI_ROOT}/imgui_draw.cpp"
    "${UNIVERSALFNAF_IMGUI_ROOT}/imgui_tables.cpp"
    "${UNIVERSALFNAF_IMGUI_ROOT}/imgui_widgets.cpp"
    "${UNIVERSALFNAF_IMGUI_ROOT}/backends/imgui_impl_win32.cpp"
    "${UNIVERSALFNAF_IMGUI_ROOT}/backends/imgui_impl_dx11.cpp")

target_include_directories(universalfnaf_imgui PUBLIC
    "${UNIVERSALFNAF_IMGUI_ROOT}"
    "${UNIVERSALFNAF_IMGUI_ROOT}/backends")

# UNICODE only: the Win32 backend expects the native windows.h defaults
# (defining WIN32_LEAN_AND_MEAN here would hide headers ImGui relies on).
target_compile_definitions(universalfnaf_imgui PUBLIC UNICODE _UNICODE)

if(MSVC)
    target_compile_options(universalfnaf_imgui PRIVATE /utf-8 /EHsc /Zc:__cplusplus /W3)
else()
    target_compile_options(universalfnaf_imgui PRIVATE -Wall)
endif()

# d3dcompiler: the DX11 backend compiles its shaders with D3DCompile at runtime.
target_link_libraries(universalfnaf_imgui PUBLIC d3d11 dxgi d3dcompiler imm32)

# ---------------------------------------------------------------------------
# Target definition
# ---------------------------------------------------------------------------
set(UNIVERSALFNAF_SOURCES
    src/main.cpp
    src/Common/WinError.cpp
    src/Common/Logger.cpp
    src/Common/Utils.cpp
    src/Common/GdiPlusSession.cpp
    src/Core/ConfigManager.cpp
    src/Net/WfpFilterManager.cpp
    src/Net/Win32TcpConnectionTerminator.cpp
    src/Process/Win32ProcessManager.cpp
    src/Input/Win32HotkeyManager.cpp
    src/Input/HotkeyCapture.cpp
    src/UI/GifAnimator.cpp
    src/UI/Renderer.cpp
    src/UI/OverlayWindow.cpp
    src/UI/UIManager.cpp
    src/App/Application.cpp
    resources/universalfnaf.rc)

add_executable(UniversalFnaf WIN32 ${UNIVERSALFNAF_SOURCES})

target_include_directories(UniversalFnaf
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${UNIVERSALFNAF_IMGUI_ROOT}"
        "${UNIVERSALFNAF_IMGUI_ROOT}/backends")

target_link_libraries(UniversalFnaf PRIVATE universalfnaf_imgui ${UNIVERSALFNAF_JSON_TARGET})

target_compile_definitions(UniversalFnaf PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
    WIN32_EXTRA_LEAN
    $<$<CONFIG:Debug>:UNIVERSALFNAF_DEBUG>)

target_link_libraries(UniversalFnaf PRIVATE
    d3d11
    dxgi
    dcomp
    dwmapi
    gdiplus
    fwpuclnt
    iphlpapi
    ws2_32
    shlwapi
    shell32
    ole32
    oleaut32
    uuid
    user32
    gdi32
    advapi32
    rpcrt4)

if(MSVC)
    target_compile_options(UniversalFnaf PRIVATE
        /utf-8            # sources are UTF-8; without this MSVC reads them as CP1251
        /W4
        /permissive-
        /EHsc
        /Zc:__cplusplus
        /Zc:preprocessor
        /guard:cf
        /Gy
        /Gw
        /MP)
    target_link_options(UniversalFnaf PRIVATE /GUARD:CF /DYNAMICBASE /NXCOMPAT)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        # LNK1246: /HIGHENTROPYVA is a 64-bit-only hardening option.
        target_link_options(UniversalFnaf PRIVATE /HIGHENTROPYVA)
    endif()
    if(UNIVERSALFNAF_MANIFEST_VIA_RC)
        # resources/universalfnaf.rc embeds app.manifest as RT_MANIFEST id 1.
        # Suppress the linker-generated manifest so exactly one manifest is used.
        target_link_options(UniversalFnaf PRIVATE /MANIFEST:NO)
    endif()
else()
    target_compile_options(UniversalFnaf PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(UniversalFnaf PRIVATE -municode -mwindows)
endif()

set_target_properties(UniversalFnaf PROPERTIES
    OUTPUT_NAME "UniversalFnaf"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:UniversalFnaf>")

# ---------------------------------------------------------------------------
# Convenience: copy the sample config next to the executable on first build
# ---------------------------------------------------------------------------
add_custom_command(TARGET UniversalFnaf POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/config/config.example.json"
            "$<TARGET_FILE_DIR:UniversalFnaf>/config.example.json"
    VERBATIM)
