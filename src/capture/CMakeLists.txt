# SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
# Copyright (c) 2026 Alonso J. Núñez (GS·RUN)
#
# Linux PipeWire screencast portal — fallback capture used when Vulkan layer
# / LD_PRELOAD injection cannot attach to the target (anti-cheat, sandboxed
# Flatpak target, etc).

add_library(tubelight_capture STATIC
    pipewire_capture.cpp
)

target_include_directories(tubelight_capture PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(tubelight_capture PUBLIC
    PkgConfig::PIPEWIRE
)
