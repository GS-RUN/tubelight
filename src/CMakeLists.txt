# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GS-RUN

add_executable(tubelight
    main.cpp
)

target_link_libraries(tubelight
    PRIVATE
        glfw
        tubelight_warnings
)

set_target_properties(tubelight PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(WIN32)
    set_target_properties(tubelight PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
endif()
