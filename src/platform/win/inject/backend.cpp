// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// tubelight_backend.dll — entry point for the DLL injected into the target
// process. DllMain is called by Windows when LoadLibrary or
// CreateRemoteThread loads the module; we then bootstrap the DX11 hook.
//
// F5: minimal scaffolding — installs the Present() hook with MinHook,
// logs activation, and forwards calls to the real Present. F7 inserts the
// 8-pass pipeline render into the hook body.

#include <windows.h>

#include <cstdio>

namespace tubelight::backend {

// Defined in dx11_hook.cpp
bool install_dx11_hook();
void remove_dx11_hook();

} // namespace tubelight::backend

namespace {

void log_attach() {
    char path[MAX_PATH] = {};
    ::GetModuleFileNameA(::GetModuleHandleA("tubelight_backend.dll"), path, MAX_PATH);
    std::fprintf(stderr, "[tubelight-backend] attached to PID %lu (module: %s)\n",
                 ::GetCurrentProcessId(), path);
}

DWORD WINAPI install_thread(LPVOID) {
    log_attach();
    if (!tubelight::backend::install_dx11_hook()) {
        std::fprintf(stderr, "[tubelight-backend] DX11 hook install failed\n");
    }
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            ::DisableThreadLibraryCalls(module);
            // Hooks must not be installed from DllMain itself (loader lock).
            // Spawn a worker thread that does it.
            ::CreateThread(nullptr, 0, install_thread, nullptr, 0, nullptr);
            break;
        case DLL_PROCESS_DETACH:
            tubelight::backend::remove_dx11_hook();
            break;
        default:
            break;
    }
    return TRUE;
}
