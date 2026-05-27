# SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
# Copyright (c) 2026 Alonso J. Núñez (GS·RUN)
#
# Builds:
#   tubelight_backend.dll  — DLL loaded into target processes; hooks
#                            IDXGISwapChain::Present via MinHook.
#   tubelight_inject.exe   — small CLI that injects backend.dll into a
#                            running process by PID using CreateRemoteThread.
#
# F5 ships these as scaffolding: the hook calls the trampoline (real Present)
# and logs a counter. F7 wires the pipeline render into the hook body.

# ------ backend.dll -----------------------------------------------------
add_library(tubelight_backend SHARED
    backend.cpp
    dx11_hook.cpp
    dx12_hook.cpp
)

set_target_properties(tubelight_backend PROPERTIES
    PREFIX ""
    OUTPUT_NAME "tubelight_backend"
)

# DX11 + DX12 + DXGI come from the Windows SDK automatically with MSVC.
target_link_libraries(tubelight_backend PRIVATE d3d11 d3d12 dxgi)

if(TARGET minhook::minhook)
    target_link_libraries(tubelight_backend PRIVATE minhook::minhook)
elseif(TARGET unofficial::minhook::minhook)
    target_link_libraries(tubelight_backend PRIVATE unofficial::minhook::minhook)
endif()

target_compile_definitions(tubelight_backend PRIVATE
    NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
)

# ------ injector.exe ----------------------------------------------------
add_executable(tubelight_inject
    injector.cpp
)

set_target_properties(tubelight_inject PROPERTIES
    OUTPUT_NAME "tubelight_inject"
)

target_compile_definitions(tubelight_inject PRIVATE
    NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
)
