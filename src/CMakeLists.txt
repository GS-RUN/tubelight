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
)

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
        tubelight_warnings
)

# epoxy linkage (Windows vcpkg vs Linux pkg-config)
if(TARGET unofficial::libepoxy::libepoxy)
    target_link_libraries(tubelight_core PUBLIC unofficial::libepoxy::libepoxy)
elseif(TARGET epoxy::epoxy)
    target_link_libraries(tubelight_core PUBLIC epoxy::epoxy)
elseif(TARGET PkgConfig::EPOXY)
    target_link_libraries(tubelight_core PUBLIC PkgConfig::EPOXY)
endif()

# Embed shader source dir into compiled binary so the runtime can find shaders/
target_compile_definitions(tubelight_core
    PUBLIC
        TUBELIGHT_SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders"
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
