# SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
# Copyright (c) 2026 Alonso J. Núñez (GS·RUN)

# ---------------------------------------------------------------------------
# tubelight_core — pipeline + shader infrastructure (linked by main + tests)
# ---------------------------------------------------------------------------
add_library(tubelight_core STATIC
    core/shader.cpp
    core/fbo.cpp
    core/texture.cpp
    core/quad.cpp
    core/pipeline.cpp
    render/backend.cpp
    render/backend_gl.cpp
    $<$<BOOL:${TUBELIGHT_BUILD_DX12}>:render/backend_d3d12.cpp>
    io/image_io.cpp
    io/shader_io.cpp
    profile/crt_profile.cpp
    profile/signal_profile.cpp
    profile/validator.cpp
    profile/profile_loader.cpp
    export/slangp_exporter.cpp
    overlay/overlay_mode_win.cpp
    overlay/overlay_mode_stub.cpp
    overlay/menu.cpp
    overlay/capture_to_disk.cpp
    overlay/settings.cpp
    overlay/folder_picker.cpp
    overlay/preset_saver.cpp
    audio/crt_audio.cpp
    $<$<BOOL:${WIN32}>:capture/wgc_capture.cpp>
)

if(WIN32)
    # windowsapp.lib brokers C++/WinRT activation (Windows.Graphics.Capture
    # + IDirect3DDevice interop). runtimeobject.lib for IInspectable.
    target_link_libraries(tubelight_core PUBLIC
        d3d11 dxgi ole32 shell32 uuid xaudio2 Magnification
        windowsapp runtimeobject
    )
endif()

if(TUBELIGHT_BUILD_DX12)
    # dcomp: DirectComposition for the Phase 4a composition swap chain
    # (flip-model + cross-process click-through overlay).
    target_link_libraries(tubelight_core PUBLIC d3d12 dcomp)
    target_compile_definitions(tubelight_core PUBLIC TUBELIGHT_HAVE_D3D12)
endif()

if(imgui_FOUND)
    target_link_libraries(tubelight_core PUBLIC imgui::imgui)
    target_compile_definitions(tubelight_core PUBLIC TUBELIGHT_HAS_IMGUI)
endif()

target_include_directories(tubelight_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${STB_INCLUDE_DIR}
)

target_link_libraries(tubelight_core
    PUBLIC
        glfw
        glm::glm
        nlohmann_json::nlohmann_json
        tubelight_warnings
)

# epoxy linkage (alias defined in top-level CMakeLists.txt)
target_link_libraries(tubelight_core PUBLIC epoxy::epoxy)

# Embed shader + profile source dirs into compiled binary so the runtime can
# find shaders/ and profiles/ without relying on CWD.
target_compile_definitions(tubelight_core
    PUBLIC
        TUBELIGHT_SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders"
        TUBELIGHT_PROFILE_DIR="${CMAKE_SOURCE_DIR}/profiles"
)

# ---------------------------------------------------------------------------
# tubelight executable
# ---------------------------------------------------------------------------
add_executable(tubelight main.cpp)

target_link_libraries(tubelight
    PRIVATE
        tubelight_core
)

set_target_properties(tubelight PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(WIN32)
    set_target_properties(tubelight PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
endif()

# Phase 3c: compile the 8 .frag + fullscreen.vert to DXIL bytecode and
# copy next to the exe (shaders/dxil/*.dxil). No-op when DX12 backend
# is disabled.
tubelight_compile_shaders(tubelight)

# Dev convenience: the user runs tubelight.exe from the repo ROOT, but the
# build outputs to ${CMAKE_BINARY_DIR}/Release. Auto-copy the freshly-built
# exe there after every build so the root run-location never goes stale
# (it once silently lagged at v0.1.6). Windows-only; the root exe + DXIL
# are gitignored. The vcpkg runtime DLLs (epoxy-0/glfw3) change only on a
# dependency bump, so they're not re-copied here — they already sit in the
# root and a bump is a one-off manual refresh.
if(WIN32)
    add_custom_command(TARGET tubelight POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:tubelight>" "${CMAKE_SOURCE_DIR}/"
        COMMENT "[deploy] sync tubelight.exe to repo root"
        VERBATIM)
    if(TUBELIGHT_BUILD_DX12)
        add_custom_command(TARGET tubelight POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                    "$<TARGET_FILE_DIR:tubelight>/shaders/dxil"
                    "${CMAKE_SOURCE_DIR}/shaders/dxil"
            COMMENT "[deploy] sync DXIL bytecodes to repo root shaders/dxil"
            VERBATIM)
    endif()
endif()
