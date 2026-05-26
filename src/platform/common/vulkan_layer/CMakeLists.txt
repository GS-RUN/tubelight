# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GS-RUN
#
# VK_LAYER_tubelight_overlay — Vulkan layer that hooks vkQueuePresentKHR.
# Works identically on Windows and Linux because the Vulkan loader is
# itself cross-platform.

add_library(VkLayer_tubelight SHARED
    layer.cpp
)

set_target_properties(VkLayer_tubelight PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "VkLayer_tubelight_overlay"
)

# Windows: the file should be named VkLayer_tubelight_overlay.dll for the
# loader to find it. Override the prefix on win.
if(WIN32)
    set_target_properties(VkLayer_tubelight PROPERTIES
        PREFIX ""
    )
endif()

target_link_libraries(VkLayer_tubelight PRIVATE Vulkan::Vulkan)

target_compile_definitions(VkLayer_tubelight PRIVATE
    VK_NO_PROTOTYPES  # we resolve everything dynamically through the loader
)

if(WIN32)
    target_compile_definitions(VkLayer_tubelight PRIVATE
        VK_USE_PLATFORM_WIN32_KHR
        NOMINMAX WIN32_LEAN_AND_MEAN
    )
elseif(UNIX)
    target_compile_definitions(VkLayer_tubelight PRIVATE
        VK_USE_PLATFORM_XLIB_KHR
        VK_USE_PLATFORM_WAYLAND_KHR
    )
endif()

# Copy the manifest JSON next to the built library so VK_ADD_LAYER_PATH
# pointing at the build directory finds both files together.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/VkLayer_tubelight_overlay.json
    ${CMAKE_BINARY_DIR}/VkLayer_tubelight_overlay.json
    COPYONLY
)
