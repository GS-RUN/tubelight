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

# Common include dir for in-tree code
add_library(tubelight_warnings INTERFACE)
target_compile_features(tubelight_warnings INTERFACE cxx_std_20)

# Subdirectories
add_subdirectory(src)

if(TUBELIGHT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Print summary
message(STATUS "Tubelight ${PROJECT_VERSION}")
message(STATUS "  Build type:      ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ compiler:    ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Tests:           ${TUBELIGHT_BUILD_TESTS}")
message(STATUS "  Installer:       ${TUBELIGHT_BUILD_INSTALLER}")
