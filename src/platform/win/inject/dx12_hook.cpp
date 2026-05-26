// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// DirectX 12 Present() hook via MinHook.
//
// Same strategy as DX11: build a throw-away ID3D12Device + IDXGIFactory4 +
// IDXGISwapChain3, extract vtable[140] (Present) and vtable[164] (Present1),
// install MinHook trampolines.
//
// F6 ships scaffolding — the hook is a passthrough + counter. F7 inserts
// the Tubelight pipeline before the swap-chain present.

#if defined(_WIN32)

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

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

using Present_t  = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT);
using Present1_t = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);

std::atomic<Present_t>  g_real_present {nullptr};
std::atomic<Present1_t> g_real_present1{nullptr};
std::atomic<unsigned long long> g_present_calls{0};
bool g_installed = false;

HRESULT STDMETHODCALLTYPE present_detour(IDXGISwapChain3* sc, UINT sync, UINT flags) {
    g_present_calls.fetch_add(1, std::memory_order_relaxed);
    auto real = g_real_present.load(std::memory_order_acquire);
    return real ? real(sc, sync, flags) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE present1_detour(IDXGISwapChain3* sc, UINT sync, UINT flags,
                                          const DXGI_PRESENT_PARAMETERS* pp) {
    g_present_calls.fetch_add(1, std::memory_order_relaxed);
    auto real = g_real_present1.load(std::memory_order_acquire);
    return real ? real(sc, sync, flags, pp) : E_FAIL;
}

bool resolve_swapchain3_vtable(void*& present_out, void*& present1_out) {
    HMODULE d3d12 = ::LoadLibraryA("d3d12.dll");
    HMODULE dxgi  = ::LoadLibraryA("dxgi.dll");
    if (!d3d12 || !dxgi) return false;

    auto pfn_create_dxgi =
        reinterpret_cast<HRESULT(WINAPI*)(REFIID, void**)>(::GetProcAddress(dxgi, "CreateDXGIFactory1"));
    auto pfn_d3d12_create_device =
        reinterpret_cast<HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**)>(
            ::GetProcAddress(d3d12, "D3D12CreateDevice"));
    if (!pfn_create_dxgi || !pfn_d3d12_create_device) return false;

    IDXGIFactory4* factory = nullptr;
    if (FAILED(pfn_create_dxgi(IID_PPV_ARGS(&factory))) || !factory) return false;

    ID3D12Device* device = nullptr;
    if (FAILED(pfn_d3d12_create_device(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) || !device) {
        factory->Release();
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC qdesc = {};
    qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* queue = nullptr;
    if (FAILED(device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&queue))) || !queue) {
        device->Release(); factory->Release();
        return false;
    }

    HWND hwnd = ::CreateWindowExA(0, "STATIC", "tubelight-tmp", WS_OVERLAPPED, 0, 0, 8, 8,
                                  nullptr, nullptr, ::GetModuleHandleA(nullptr), nullptr);

    DXGI_SWAP_CHAIN_DESC1 sc_desc = {};
    sc_desc.Width  = 8;
    sc_desc.Height = 8;
    sc_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sc_desc.SampleDesc.Count = 1;
    sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc_desc.BufferCount = 2;
    sc_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    IDXGISwapChain1* sc1 = nullptr;
    HRESULT hr = factory->CreateSwapChainForHwnd(queue, hwnd, &sc_desc, nullptr, nullptr, &sc1);
    if (FAILED(hr) || !sc1) {
        if (hwnd) ::DestroyWindow(hwnd);
        queue->Release(); device->Release(); factory->Release();
        return false;
    }

    IDXGISwapChain3* sc3 = nullptr;
    sc1->QueryInterface(IID_PPV_ARGS(&sc3));

    if (sc3) {
        void** vtable = *reinterpret_cast<void***>(sc3);
        // IDXGISwapChain3 inherits IDXGISwapChain2/1/IDXGIDeviceSubObject/IDXGIObject/IUnknown.
        // Present  = vtable index 8 (same as IDXGISwapChain).
        // Present1 = vtable index 22 (IDXGISwapChain1::Present1).
        present_out  = vtable[8];
        present1_out = vtable[22];
        sc3->Release();
    }

    sc1->Release();
    if (hwnd) ::DestroyWindow(hwnd);
    queue->Release(); device->Release(); factory->Release();
    return present_out != nullptr;
}

} // namespace

bool install_dx12_hook() {
#if TUBELIGHT_HAS_MINHOOK
    if (g_installed) return true;
    if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED) {
        return false;
    }
    void* present_ptr  = nullptr;
    void* present1_ptr = nullptr;
    if (!resolve_swapchain3_vtable(present_ptr, present1_ptr) || !present_ptr) {
        std::fprintf(stderr, "[tubelight-backend] dx12 vtable resolution failed\n");
        return false;
    }
    void* tramp = nullptr;
    if (MH_CreateHook(present_ptr, reinterpret_cast<void*>(&present_detour), &tramp) != MH_OK) {
        return false;
    }
    g_real_present.store(reinterpret_cast<Present_t>(tramp), std::memory_order_release);

    if (present1_ptr) {
        void* tramp1 = nullptr;
        if (MH_CreateHook(present1_ptr, reinterpret_cast<void*>(&present1_detour), &tramp1) == MH_OK) {
            g_real_present1.store(reinterpret_cast<Present1_t>(tramp1), std::memory_order_release);
        }
    }
    MH_EnableHook(MH_ALL_HOOKS);
    g_installed = true;
    std::fprintf(stderr, "[tubelight-backend] DX12 Present() hook installed\n");
    return true;
#else
    return false;
#endif
}

void remove_dx12_hook() {
#if TUBELIGHT_HAS_MINHOOK
    if (g_installed) {
        // MinHook teardown is shared with dx11_hook; we let the DX11 path
        // call Uninitialize.
        g_installed = false;
    }
#endif
}

} // namespace tubelight::backend

#endif
