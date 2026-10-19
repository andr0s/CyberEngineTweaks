# macOS build of Cyber Engine Tweaks: a RED4ext plugin (red4ext/plugins/CyberEngineTweaks/CyberEngineTweaks.dylib)
# for Cyberpunk 2077 2.3.1 (Steam, Apple silicon) with the macOS port of RED4ext. The Windows build is xmake.lua.
#
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j8
#
# Dependencies are the git checkouts under vendor/ (see macos/PORT_NOTES.md for the pinned versions).

cmake_minimum_required(VERSION 3.23)
project(CyberEngineTweaks LANGUAGES C CXX OBJC OBJCXX)

if(NOT APPLE)
    message(FATAL_ERROR "This CMake build is for macOS. Build the Windows version with xmake (xmake.lua).")
endif()

set(CMAKE_OSX_ARCHITECTURES arm64)
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_OBJCXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

set(RED4EXT_SDK_DIR "${CMAKE_SOURCE_DIR}/vendor/RED4ext.SDK-macos" CACHE PATH "RED4ext.SDK-macos checkout (jackmaxwil/RED4ext.SDK-macos)")
set(VENDOR_DIR "${CMAKE_SOURCE_DIR}/vendor")

# ---- version (src/xmake/CETVersion.h.in on Windows) ----------------------------------------------------------------

set(CET_GIT_TAG "v1.37.1" CACHE STRING "CET version tag")
set(CET_GIT_TAG_LONG "${CET_GIT_TAG}")
set(CET_GIT_BRANCH "macos")
set(CET_GIT_COMMIT "")
set(CET_GIT_COMMIT_LONG "")
set(CET_GIT_COMMIT_DATE "")
string(REGEX MATCH "([0-9]+)\\.([0-9]+)\\.([0-9]+)" _cet_version "${CET_GIT_TAG}")
set(CET_VERSION_MAJOR ${CMAKE_MATCH_1})
set(CET_VERSION_MINOR ${CMAKE_MATCH_2})
set(CET_VERSION_PATCH ${CMAKE_MATCH_3})
string(TIMESTAMP CET_CURRENT_YEAR "%Y")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CET_DEBUG_BUILD 1)
else()
    set(CET_DEBUG_BUILD 0)
endif()
configure_file(macos/CETVersion.h.in "${CMAKE_BINARY_DIR}/generated/config/CETVersion.h" @ONLY)

# TiltedCore headers are included as <TiltedCore/...>
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
file(CREATE_LINK "${VENDOR_DIR}/TiltedCore/Code/core/include" "${CMAKE_BINARY_DIR}/generated/TiltedCore" SYMBOLIC)

# ---- LuaJIT ----------------------------------------------------------------------------------------------------------
# Built with its own makefile from a copy of vendor/luajit. The JIT compiler is disabled: no executable memory is
# created at runtime (the interpreter is still LuaJIT's). The FFI stays compiled in because CET passes 64-bit integers to
# Lua as cdata (123ull); mods cannot reach the ffi module from the sandbox.

include(ExternalProject)
set(LUAJIT_BUILD_DIR "${CMAKE_BINARY_DIR}/luajit")
ExternalProject_Add(luajit_build
    SOURCE_DIR "${VENDOR_DIR}/luajit"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E copy_directory "${VENDOR_DIR}/luajit" "${LUAJIT_BUILD_DIR}"
    # Architecture and SDK are explicit: under an x86_64 CMake (Rosetta) the makefile's plain `cc` would build for
    # x86_64, and the compiler called by path does not find the SDK on its own.
    BUILD_COMMAND make -C "${LUAJIT_BUILD_DIR}/src" luajit.h libluajit.a "CC=${CMAKE_C_COMPILER} -arch arm64 -isysroot ${CMAKE_OSX_SYSROOT}" "MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}" "XCFLAGS=-DLUAJIT_DISABLE_JIT" BUILDMODE=static
    BUILD_IN_SOURCE OFF
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS "${LUAJIT_BUILD_DIR}/src/libluajit.a" "${LUAJIT_BUILD_DIR}/src/luajit.h")
add_library(luajit STATIC IMPORTED)
set_target_properties(luajit PROPERTIES IMPORTED_LOCATION "${LUAJIT_BUILD_DIR}/src/libluajit.a")
add_dependencies(luajit luajit_build)

# ---- Dear ImGui ------------------------------------------------------------------------------------------------------

add_library(cet_imgui STATIC
    "${VENDOR_DIR}/imgui/imgui.cpp"
    "${VENDOR_DIR}/imgui/imgui_demo.cpp"
    "${VENDOR_DIR}/imgui/imgui_draw.cpp"
    "${VENDOR_DIR}/imgui/imgui_tables.cpp"
    "${VENDOR_DIR}/imgui/imgui_widgets.cpp")
target_include_directories(cet_imgui PUBLIC "${VENDOR_DIR}/imgui")
# CET uses 32-bit ImWchar (wchar32) for the Material Design icon range, as in its xmake build
target_compile_definitions(cet_imgui PUBLIC "IMGUI_USER_CONFIG=\"${CMAKE_SOURCE_DIR}/src/imgui_impl/imgui_user_config.h\"" IMGUI_USE_WCHAR32)

# ---- TiltedCore (mimalloc replaced by the system allocator, see src/platform/macos/MimallocAllocatorShim.cpp) ------

file(GLOB TILTEDCORE_SOURCES "${VENDOR_DIR}/TiltedCore/Code/core/src/*.cpp")
list(FILTER TILTEDCORE_SOURCES EXCLUDE REGEX "MimallocAllocator\\.cpp$")
add_library(cet_tiltedcore STATIC ${TILTEDCORE_SOURCES} src/platform/macos/MimallocAllocatorShim.cpp)
target_include_directories(cet_tiltedcore PUBLIC "${VENDOR_DIR}/TiltedCore/Code/core/include" "${VENDOR_DIR}/hopscotch-map/include")

# ---- Cyber Engine Tweaks ---------------------------------------------------------------------------------------------

file(GLOB_RECURSE CET_SOURCES CONFIGURE_DEPENDS "src/*.cpp" "src/*.mm")
# Windows-only parts: DLL entry, Direct3D 12 renderer and hooks, Win32 window and input, x64 game patches.
list(FILTER CET_SOURCES EXCLUDE REGEX "/src/dllmain\\.cpp$")
list(FILTER CET_SOURCES EXCLUDE REGEX "/src/d3d12/D3D12\\.cpp$")
list(FILTER CET_SOURCES EXCLUDE REGEX "/src/d3d12/D3D12_Hooks\\.cpp$")
list(FILTER CET_SOURCES EXCLUDE REGEX "/src/reverse/RenderContext\\.cpp$")
list(FILTER CET_SOURCES EXCLUDE REGEX "/src/imgui_impl/(dx12|win32)\\.cpp$")
list(FILTER CET_SOURCES EXCLUDE REGEX "/src/window/window\\.cpp$")
list(FILTER CET_SOURCES EXCLUDE REGEX "/src/patches/")
list(FILTER CET_SOURCES EXCLUDE REGEX "/src/platform/macos/MimallocAllocatorShim\\.cpp$")

add_library(CyberEngineTweaks SHARED ${CET_SOURCES} "${VENDOR_DIR}/fishhook/fishhook.c")
add_dependencies(CyberEngineTweaks luajit_build)
set_target_properties(CyberEngineTweaks PROPERTIES PREFIX "" OUTPUT_NAME "CyberEngineTweaks")

target_include_directories(CyberEngineTweaks PRIVATE
    src
    "${CMAKE_BINARY_DIR}/generated"
    "${RED4EXT_SDK_DIR}/include"
    "${VENDOR_DIR}/sol2/include"
    "${LUAJIT_BUILD_DIR}/src"
    "${VENDOR_DIR}/luajit/src"
    "${VENDOR_DIR}/spdlog/include"
    "${VENDOR_DIR}/json/single_include"
    "${VENDOR_DIR}/stb"
    "${VENDOR_DIR}/fishhook")

# (RED4ext.SDK-macos defines RED4EXT_HEADER_ONLY itself)
target_compile_definitions(CyberEngineTweaks PRIVATE
    SOL_ALL_SAFETIES_ON
    SOL_LUAJIT=1
    SOL_EXCEPTIONS_SAFE_PROPAGATION
    $<$<CONFIG:Debug>:CET_DEBUG>)

target_precompile_headers(CyberEngineTweaks PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_SOURCE_DIR}/src/stdafx.h>")

foreach(_source ${CET_SOURCES})
    if(_source MATCHES "\\.mm$")
        set_source_files_properties(${_source} PROPERTIES COMPILE_OPTIONS "-fobjc-arc;-Wno-deprecated-declarations")
    else()
        set_source_files_properties(${_source} PROPERTIES COMPILE_OPTIONS "-Wno-deprecated-declarations")
    endif()
endforeach()

target_link_libraries(CyberEngineTweaks PRIVATE
    cet_imgui
    cet_tiltedcore
    luajit
    sqlite3
    "-framework Foundation"
    "-framework AppKit"
    "-framework Carbon"
    "-framework CoreGraphics"
    "-framework Metal"
    "-framework QuartzCore")

# The game and RED4ext provide nothing CET links against at build time; game addresses resolve at runtime through
# RED4ext_ResolveAddress.
target_link_options(CyberEngineTweaks PRIVATE "-Wl,-undefined,error")

# ---- unit tests for the macOS-specific security code (path jail, SQLite authorizer, UTF conversion) -----------------

option(CET_BUILD_TESTS "Build the macOS unit tests" ON)
if(CET_BUILD_TESTS)
    enable_testing()
    add_executable(cet_test_pathjail macos/tests/PathJailTests.cpp)
    target_include_directories(cet_test_pathjail PRIVATE src)
    add_test(NAME pathjail COMMAND cet_test_pathjail)

    add_executable(cet_test_sqlite_authorizer macos/tests/SqliteAuthorizerTests.cpp)
    target_include_directories(cet_test_sqlite_authorizer PRIVATE src/lsqlite3)
    target_link_libraries(cet_test_sqlite_authorizer PRIVATE sqlite3)
    add_test(NAME sqlite_authorizer COMMAND cet_test_sqlite_authorizer)

    add_executable(cet_test_utf macos/tests/UtfTests.cpp)
    target_include_directories(cet_test_utf PRIVATE src)
    add_test(NAME utf COMMAND cet_test_utf)
endif()
