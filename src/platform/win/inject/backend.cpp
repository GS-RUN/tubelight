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

// Defined in dx12_hook.cpp
bool install_dx12_hook();
void remove_dx12_hook();

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
    bool dx11 = tubelight::backend::install_dx11_hook();
    bool dx12 = tubelight::backend::install_dx12_hook();
    if (!dx11 && !dx12) {
        std::fprintf(stderr, "[tubelight-backend] both DX11 and DX12 hooks failed to install\n");
    } else {
        std::fprintf(stderr, "[tubelight-backend] hooks: DX11=%s DX12=%s\n",
                     dx11 ? "ok" : "fail", dx12 ? "ok" : "fail");
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
            tubelight::backend::remove_dx12_hook();
            tubelight::backend::remove_dx11_hook();
            break;
        default:
            break;
    }
    return TRUE;
}
