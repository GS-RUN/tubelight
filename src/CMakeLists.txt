# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GS-RUN

# ---------------------------------------------------------------------------
# tubelight_core — pipeline + shader infrastructure (linked by main + tests)
# ---------------------------------------------------------------------------
add_library(tubelight_core STATIC
    core/shader.cpp
    core/fbo.cpp
    core/texture.cpp
    core/quad.cpp
    core/pipeline.cpp
    io/image_io.cpp
    io/shader_io.cpp
    profile/crt_profile.cpp
    profile/signal_profile.cpp
    profile/validator.cpp
    profile/profile_loader.cpp
    ipc/ipc_unix.cpp
    ipc/ipc_win.cpp
    export/slangp_exporter.cpp
    overlay/overlay_mode_win.cpp
    overlay/overlay_mode_stub.cpp
    overlay/menu.cpp
    overlay/capture_to_disk.cpp
    overlay/settings.cpp
    overlay/folder_picker.cpp
    overlay/preset_saver.cpp
    audio/crt_audio.cpp
)

if(WIN32)
    target_link_libraries(tubelight_core PUBLIC d3d11 dxgi ole32 shell32 uuid xaudio2)
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
