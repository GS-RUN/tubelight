# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GS-RUN

cmake_minimum_required(VERSION 3.26)

project(tubelight
    VERSION 0.1.0
    DESCRIPTION "High-fidelity CRT overlay for Windows and Linux"
    HOMEPAGE_URL "https://github.com/gs-run/tubelight"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default build type
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Warnings (C1 + general hygiene)
if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter)
endif()

# Options
option(TUBELIGHT_BUILD_TESTS "Build tubelight tests" OFF)
option(TUBELIGHT_BUILD_INSTALLER "Build platform installer (NSIS / AppImage)" OFF)

# Generate compile_commands.json for clangd / editors
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find dependencies
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG QUIET)
if(NOT glm_FOUND)
    find_package(glm REQUIRED)
endif()

# libepoxy: GL function loader.
# Linux: pkg-config (libepoxy-dev installs pkgconfig file).
# Windows: vcpkg installs only the .pc file (no CMake config), so we do a
# manual find_library + find_path and create an IMPORTED target.
include(FindPkgConfig OPTIONAL)
if(WIN32)
    find_path(EPOXY_INCLUDE_DIR NAMES epoxy/gl.h)
    find_library(EPOXY_LIBRARY NAMES epoxy)
    if(NOT EPOXY_INCLUDE_DIR OR NOT EPOXY_LIBRARY)
        message(FATAL_ERROR "libepoxy headers/library not found (vcpkg libepoxy should provide them).")
    endif()
    add_library(tubelight_epoxy INTERFACE)
    target_include_directories(tubelight_epoxy INTERFACE "${EPOXY_INCLUDE_DIR}")
    target_link_libraries(tubelight_epoxy INTERFACE "${EPOXY_LIBRARY}")
    add_library(epoxy::epoxy ALIAS tubelight_epoxy)
else()
    pkg_check_modules(EPOXY REQUIRED IMPORTED_TARGET epoxy)
    add_library(epoxy_alias INTERFACE)
    target_link_libraries(epoxy_alias INTERFACE PkgConfig::EPOXY)
    add_library(epoxy::epoxy ALIAS epoxy_alias)
endif()

# stb (header-only). System package provides <stb_image.h> on include path; on Windows vcpkg installs to include/.
find_path(STB_INCLUDE_DIR NAMES stb_image.h PATH_SUFFIXES stb)
if(NOT STB_INCLUDE_DIR)
    message(FATAL_ERROR "stb_image.h not found. Install libstb-dev / stb_image-devel / stb / vcpkg stb.")
endif()

# nlohmann-json
find_package(nlohmann_json CONFIG REQUIRED)

# MinHook (Windows only) for DX11/DX12/OpenGL Present hook
if(WIN32)
    find_package(minhook CONFIG QUIET)
    if(NOT minhook_FOUND)
        find_package(unofficial-minhook CONFIG QUIET)
    endif()
endif()

# Common include dir for in-tree code
add_library(tubelight_warnings INTERFACE)
target_compile_features(tubelight_warnings INTERFACE cxx_std_20)

# Subdirectories
add_subdirectory(src)

# Platform-specific injection / preload modules
if(UNIX AND NOT APPLE)
    add_subdirectory(src/platform/linux/preload)
endif()
if(WIN32)
    add_subdirectory(src/platform/win/inject)
endif()

# Vulkan layer (common to Windows and Linux). Requires Vulkan headers.
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    add_subdirectory(src/platform/common/vulkan_layer)
else()
    message(WARNING "Vulkan SDK not found; skipping VK_LAYER_tubelight_overlay build")
endif()

# PipeWire fallback (Linux only)
if(UNIX AND NOT APPLE)
    if(COMMAND pkg_check_modules)
        pkg_check_modules(PIPEWIRE IMPORTED_TARGET libpipewire-0.3 QUIET)
        if(PIPEWIRE_FOUND)
            add_subdirectory(src/capture)
        else()
            message(WARNING "PipeWire not found; capture fallback unavailable")
        endif()
    endif()
endif()

if(TUBELIGHT_BUILD_TESTS)
    # tests/CMakeLists.txt is added in v1.1 (golden harness in F7 follow-up).
    # For now we silently skip if the file is missing.
    if(EXISTS "${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt")
        enable_testing()
        add_subdirectory(tests)
    else()
        message(STATUS "tests/CMakeLists.txt not present yet — skipping test target.")
    endif()
endif()

# Print summary
message(STATUS "Tubelight ${PROJECT_VERSION}")
message(STATUS "  Build type:      ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ compiler:    ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Tests:           ${TUBELIGHT_BUILD_TESTS}")
message(STATUS "  Installer:       ${TUBELIGHT_BUILD_INSTALLER}")
