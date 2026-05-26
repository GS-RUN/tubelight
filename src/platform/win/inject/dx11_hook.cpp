// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// DirectX 11 Present() hook via MinHook.
//
// Strategy (matches what ReShade / Special K do):
//   1. Create a throw-away device + swap chain so we can read the real
//      function pointer for IDXGISwapChain::Present from its vtable.
//   2. Install a MinHook detour on that pointer pair.
//   3. The detour stores the trampoline pointer and routes incoming calls
//      through it after our own logic. F7 inserts the Tubelight pipeline
//      render here.
//
// This file is Windows-only. The CMakeLists guard prevents it from being
// compiled on Linux.

#if defined(_WIN32)

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <atomic>
#include <cstdio>

#if __has_include(<MinHook.h>)
  #include <MinHook.h>
  #define TUBELIGHT_HAS_MINHOOK 1
#else
  #define TUBELIGHT_HAS_MINHOOK 0
#endif

namespace tubelight::backend {

namespace {

using PresentFn = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);

std::atomic<PresentFn> g_real_present{nullptr};
std::atomic<unsigned long long> g_present_calls{0};
bool g_installed = false;

HRESULT STDMETHODCALLTYPE present_detour(IDXGISwapChain* swap_chain, UINT sync_interval, UINT flags) {
    g_present_calls.fetch_add(1, std::memory_order_relaxed);
    // F7: pipeline.render_to_screen(back_buffer) goes here, before the swap.
    auto real = g_real_present.load(std::memory_order_acquire);
    if (real) {
        return real(swap_chain, sync_interval, flags);
    }
    return E_FAIL;
}

bool resolve_present_vtable_entry(void*& out_present_ptr) {
    // Create a temporary device + swap chain to extract the vtable.
    HWND tmp = ::CreateWindowExA(0, "STATIC", "tubelight-tmp", WS_OVERLAPPED, 0, 0, 8, 8,
                                 nullptr, nullptr, ::GetModuleHandleA(nullptr), nullptr);
    if (!tmp) return false;

    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferCount = 1;
    desc.BufferDesc.Width  = 8;
    desc.BufferDesc.Height = 8;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count  = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = tmp;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ID3D11Device*        dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain*      sc  = nullptr;
    D3D_FEATURE_LEVEL    fl;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0, nullptr, 0, D3D11_SDK_VERSION,
        &desc, &sc, &dev, &fl, &ctx);

    if (FAILED(hr) || !sc) {
        ::DestroyWindow(tmp);
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(sc);
    // IDXGISwapChain::Present is index 8 in the vtable.
    out_present_ptr = vtable[8];

    if (ctx) ctx->Release();
    if (sc)  sc->Release();
    if (dev) dev->Release();
    ::DestroyWindow(tmp);
    return true;
}

} // namespace

bool install_dx11_hook() {
#if TUBELIGHT_HAS_MINHOOK
    if (g_installed) return true;
    if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED) {
        return false;
    }

    void* present_ptr = nullptr;
    if (!resolve_present_vtable_entry(present_ptr) || !present_ptr) {
        std::fprintf(stderr, "[tubelight-backend] failed to resolve IDXGISwapChain::Present vtable\n");
        return false;
    }

    void* trampoline = nullptr;
    MH_STATUS s = MH_CreateHook(present_ptr,
                                reinterpret_cast<void*>(&present_detour),
                                &trampoline);
    if (s != MH_OK) {
        std::fprintf(stderr, "[tubelight-backend] MH_CreateHook failed: %d\n", s);
        return false;
    }
    g_real_present.store(reinterpret_cast<PresentFn>(trampoline), std::memory_order_release);

    if (MH_EnableHook(present_ptr) != MH_OK) {
        std::fprintf(stderr, "[tubelight-backend] MH_EnableHook failed\n");
        return false;
    }
    g_installed = true;
    std::fprintf(stderr, "[tubelight-backend] DX11 Present() hook installed\n");
    return true;
#else
    std::fprintf(stderr, "[tubelight-backend] MinHook header not found at build time; hook disabled\n");
    return false;
#endif
}

void remove_dx11_hook() {
#if TUBELIGHT_HAS_MINHOOK
    if (g_installed) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        g_installed = false;
    }
    auto n = g_present_calls.load(std::memory_order_relaxed);
    if (n) {
        std::fprintf(stderr, "[tubelight-backend] Present() called %llu times during session\n", n);
    }
#endif
}

} // namespace tubelight::backend

#endif // _WIN32
