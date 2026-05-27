# SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
# Copyright (c) 2026 Alonso J. Núñez (GS·RUN)
#
# Builds libtubelight_preload.so for use via LD_PRELOAD on Linux. The .so
# intercepts glXSwapBuffers (X11/GLX) and eglSwapBuffers (Wayland/EGL),
# logs the swap, and forwards to the real implementation via
# dlsym(RTLD_NEXT, ...).
#
# F5 ships with a passthrough hook that proves the technique works on the
# user's target processes. F7 wires the full pipeline into the hook.

add_library(tubelight_preload SHARED
    preload.cpp
)

set_target_properties(tubelight_preload PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "tubelight_preload"
    POSITION_INDEPENDENT_CODE ON
)

target_link_libraries(tubelight_preload PRIVATE dl)

target_compile_definitions(tubelight_preload PRIVATE
    _GNU_SOURCE
)
