// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)

#include "render/backend_d3d12.h"

#if defined(TUBELIGHT_HAVE_D3D12)

#include <cstdio>
#include <cstring>

namespace tubelight {

namespace {

using Microsoft::WRL::ComPtr;

const char* feature_level_name(D3D_FEATURE_LEVEL fl) {
    switch (fl) {
        case D3D_FEATURE_LEVEL_12_2: return "FL 12_2";
        case D3D_FEATURE_LEVEL_12_1: return "FL 12_1";
        case D3D_FEATURE_LEVEL_12_0: return "FL 12_0";
        case D3D_FEATURE_LEVEL_11_1: return "FL 11_1";
        case D3D_FEATURE_LEVEL_11_0: return "FL 11_0";
        default:                     return "FL ?";
    }
}

// Pick the first hardware adapter that supports at least FL 12_0 and rank
// by dedicated VRAM via the high-performance preference flag (DXGI 1.6).
HRESULT pick_hardware_adapter(IDXGIFactory6* factory,
                              ComPtr<IDXGIAdapter1>& out_adapter,
                              D3D_FEATURE_LEVEL& out_feature_level) {
    static const D3D_FEATURE_LEVEL kLevels[] = {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
    };

    for (UINT idx = 0;; ++idx) {
        ComPtr<IDXGIAdapter1> adapter;
        HRESULT hr = factory->EnumAdapterByGpuPreference(
            idx,
            DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&adapter));
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(hr)) continue;

        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

        for (D3D_FEATURE_LEVEL fl : kLevels) {
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), fl, _uuidof(ID3D12Device), nullptr))) {
                out_adapter        = adapter;
                out_feature_level  = fl;
                return S_OK;
            }
        }
    }
    return E_FAIL;
}

} // namespace

D3D12Backend::~D3D12Backend() {
    shutdown();
}

bool D3D12Backend::init(const BackendInitParams& params) {
    if (ready_) return true;
    if (!params.native_window_handle) {
        std::fprintf(stderr, "[tubelight][d3d12] init: native_window_handle is null\n");
        return false;
    }
    if (params.width <= 0 || params.height <= 0) {
        std::fprintf(stderr, "[tubelight][d3d12] init: invalid size %dx%d\n",
                     params.width, params.height);
        return false;
    }
    hwnd_   = static_cast<HWND>(params.native_window_handle);
    width_  = params.width;
    height_ = params.height;

    UINT factory_flags = 0;
    if (params.enable_debug) {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
            std::fprintf(stderr, "[tubelight][d3d12] debug layer enabled\n");
        } else {
            std::fprintf(stderr, "[tubelight][d3d12] debug layer requested but not available\n");
        }
    }

    if (FAILED(CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&dxgi_factory_)))) {
        std::fprintf(stderr, "[tubelight][d3d12] CreateDXGIFactory2 failed\n");
        return false;
    }

    ComPtr<IDXGIAdapter1> adapter;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_12_0;
    if (FAILED(pick_hardware_adapter(dxgi_factory_.Get(), adapter, fl))) {
        std::fprintf(stderr,
            "[tubelight][d3d12] no DX12-capable hardware adapter found "
            "(Intel HD 4400 without recent driver, or pre-2015 GPU?)\n");
        return false;
    }

    DXGI_ADAPTER_DESC1 adesc{};
    adapter->GetDesc1(&adesc);
    char adapter_desc_utf8[128] = {0};
    WideCharToMultiByte(CP_UTF8, 0, adesc.Description, -1,
                        adapter_desc_utf8, sizeof(adapter_desc_utf8) - 1,
                        nullptr, nullptr);
    std::snprintf(name_buf_, sizeof(name_buf_),
                  "Direct3D 12 (%s, %s)", adapter_desc_utf8, feature_level_name(fl));

    if (FAILED(D3D12CreateDevice(adapter.Get(), fl, IID_PPV_ARGS(&device_)))) {
        std::fprintf(stderr, "[tubelight][d3d12] D3D12CreateDevice failed on chosen adapter\n");
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    if (FAILED(device_->CreateCommandQueue(&qd, IID_PPV_ARGS(&cmd_queue_)))) {
        std::fprintf(stderr, "[tubelight][d3d12] CreateCommandQueue failed\n");
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
    rtv_desc.NumDescriptors = kBackBufferCount;
    rtv_desc.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device_->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv_heap_)))) {
        std::fprintf(stderr, "[tubelight][d3d12] CreateDescriptorHeap (RTV) failed\n");
        return false;
    }
    rtv_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                IID_PPV_ARGS(&cmd_alloc_)))) {
        std::fprintf(stderr, "[tubelight][d3d12] CreateCommandAllocator failed\n");
        return false;
    }
    if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           cmd_alloc_.Get(), nullptr,
                                           IID_PPV_ARGS(&cmd_list_)))) {
        std::fprintf(stderr, "[tubelight][d3d12] CreateCommandList failed\n");
        return false;
    }
    cmd_list_->Close(); // start closed; begin_frame() resets

    if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) {
        std::fprintf(stderr, "[tubelight][d3d12] CreateFence failed\n");
        return false;
    }
    fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fence_event_) {
        std::fprintf(stderr, "[tubelight][d3d12] CreateEvent failed\n");
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width       = static_cast<UINT>(width_);
    scd.Height      = static_cast<UINT>(height_);
    scd.Format      = kBackBufferFormat;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = kBackBufferCount;
    scd.Scaling     = DXGI_SCALING_NONE;
    scd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode   = DXGI_ALPHA_MODE_IGNORE;
    scd.Flags       = 0;

    ComPtr<IDXGISwapChain1> sc1;
    if (FAILED(dxgi_factory_->CreateSwapChainForHwnd(cmd_queue_.Get(), hwnd_, &scd,
                                                      nullptr, nullptr, &sc1))) {
        std::fprintf(stderr, "[tubelight][d3d12] CreateSwapChainForHwnd failed\n");
        return false;
    }
    // Block ALT+ENTER fullscreen toggle handled by DXGI — Tubelight owns
    // its own fullscreen state machine in overlay/.
    dxgi_factory_->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
    if (FAILED(sc1.As(&swap_chain_))) {
        std::fprintf(stderr, "[tubelight][d3d12] swap chain QueryInterface to IDXGISwapChain4 failed\n");
        return false;
    }

    if (!create_swap_chain_resources(width_, height_)) {
        return false;
    }

    current_back_buffer_ = swap_chain_->GetCurrentBackBufferIndex();
    ready_ = true;
    std::fprintf(stderr, "[tubelight][d3d12] init OK on %s (%dx%d, %u backbuffers)\n",
                 name_buf_, width_, height_, kBackBufferCount);
    return true;
}

bool D3D12Backend::create_swap_chain_resources(int /*width*/, int /*height*/) {
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < kBackBufferCount; ++i) {
        if (FAILED(swap_chain_->GetBuffer(i, IID_PPV_ARGS(&back_buffers_[i])))) {
            std::fprintf(stderr, "[tubelight][d3d12] swap_chain_->GetBuffer(%u) failed\n", i);
            return false;
        }
        device_->CreateRenderTargetView(back_buffers_[i].Get(), nullptr, rtv);
        rtv.ptr += rtv_descriptor_size_;
    }
    return true;
}

void D3D12Backend::destroy_swap_chain_resources() {
    for (UINT i = 0; i < kBackBufferCount; ++i) {
        back_buffers_[i].Reset();
    }
}

void D3D12Backend::wait_for_gpu_idle() {
    if (!cmd_queue_ || !fence_) return;
    const UINT64 target = ++fence_value_;
    if (FAILED(cmd_queue_->Signal(fence_.Get(), target))) return;
    if (fence_->GetCompletedValue() < target) {
        fence_->SetEventOnCompletion(target, fence_event_);
        WaitForSingleObject(fence_event_, INFINITE);
    }
}

void D3D12Backend::shutdown() {
    if (!ready_) {
        if (fence_event_) { CloseHandle(fence_event_); fence_event_ = nullptr; }
        return;
    }
    wait_for_gpu_idle();
    destroy_swap_chain_resources();
    swap_chain_.Reset();
    cmd_list_.Reset();
    cmd_alloc_.Reset();
    rtv_heap_.Reset();
    fence_.Reset();
    cmd_queue_.Reset();
    device_.Reset();
    dxgi_factory_.Reset();
    if (fence_event_) { CloseHandle(fence_event_); fence_event_ = nullptr; }
    ready_ = false;
}

void D3D12Backend::resize(int width, int height) {
    if (!ready_ || (width == width_ && height == height_) || width <= 0 || height <= 0) {
        return;
    }
    wait_for_gpu_idle();
    destroy_swap_chain_resources();
    if (FAILED(swap_chain_->ResizeBuffers(kBackBufferCount,
                                           static_cast<UINT>(width),
                                           static_cast<UINT>(height),
                                           kBackBufferFormat, 0))) {
        std::fprintf(stderr, "[tubelight][d3d12] swap_chain_->ResizeBuffers failed\n");
        return;
    }
    width_  = width;
    height_ = height;
    create_swap_chain_resources(width_, height_);
    current_back_buffer_ = swap_chain_->GetCurrentBackBufferIndex();
}

void D3D12Backend::begin_frame() {
    if (!ready_ || in_frame_) return;
    current_back_buffer_ = swap_chain_->GetCurrentBackBufferIndex();
    cmd_alloc_->Reset();
    cmd_list_->Reset(cmd_alloc_.Get(), nullptr);

    D3D12_RESOURCE_BARRIER b{};
    b.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    b.Transition.pResource   = back_buffers_[current_back_buffer_].Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd_list_->ResourceBarrier(1, &b);

    in_frame_           = true;
    default_fb_bound_   = false;
}

void D3D12Backend::bind_default_framebuffer() {
    if (!ready_ || !in_frame_) return;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += current_back_buffer_ * rtv_descriptor_size_;
    cmd_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    default_fb_bound_ = true;
}

void D3D12Backend::set_viewport(int x, int y, int w, int h) {
    if (!ready_ || !in_frame_) return;
    D3D12_VIEWPORT vp{ static_cast<FLOAT>(x), static_cast<FLOAT>(y),
                       static_cast<FLOAT>(w), static_cast<FLOAT>(h), 0.0f, 1.0f };
    D3D12_RECT sr{ x, y, x + w, y + h };
    cmd_list_->RSSetViewports(1, &vp);
    cmd_list_->RSSetScissorRects(1, &sr);
}

void D3D12Backend::clear_color(float r, float g, float b, float a) {
    if (!ready_ || !in_frame_) return;
    if (!default_fb_bound_) bind_default_framebuffer();
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += current_back_buffer_ * rtv_descriptor_size_;
    const FLOAT c[4] = { r, g, b, a };
    cmd_list_->ClearRenderTargetView(rtv, c, 0, nullptr);
}

void D3D12Backend::draw_fullscreen_quad() {
    // Phase 3c lands the PSO + HLSL fullscreen-triangle VS. In 3b the
    // skeleton has no shaders / root signature so this is a no-op; the
    // call is allowed (Pipeline does check supports_pipeline() before
    // invoking) but quietly does nothing for code that calls it manually.
}

// ----- Phase 3c handle API — F3c-2 stubs; F3c-4 real impl ---------------

namespace {
void warn_unimplemented_once(const char* method) {
    static bool warned = false;
    if (warned) return;
    warned = true;
    std::fprintf(stderr,
        "[tubelight][d3d12] %s called — Phase 3c handle API not yet "
        "implemented (F3c-4 pending). supports_pipeline() returns false; "
        "callers should check that first.\n",
        method);
}
} // namespace

TextureHandle D3D12Backend::create_texture(const TextureDesc&) {
    warn_unimplemented_once("create_texture");
    return {0};
}
RenderTargetHandle D3D12Backend::create_render_target(int, int, PixelFormat) {
    warn_unimplemented_once("create_render_target");
    return {0};
}
PassHandle D3D12Backend::create_pass(const PassDesc&) {
    warn_unimplemented_once("create_pass");
    return {0};
}
void D3D12Backend::destroy_texture(TextureHandle) {}
void D3D12Backend::destroy_render_target(RenderTargetHandle) {}
void D3D12Backend::destroy_pass(PassHandle) {}
bool D3D12Backend::upload_texture_rgba8(TextureHandle, const void*, int, int) {
    warn_unimplemented_once("upload_texture_rgba8");
    return false;
}
void D3D12Backend::copy_rt_to_texture(RenderTargetHandle, TextureHandle) {
    warn_unimplemented_once("copy_rt_to_texture");
}
void D3D12Backend::bind_render_target(RenderTargetHandle) {
    warn_unimplemented_once("bind_render_target");
}
void D3D12Backend::bind_pass(PassHandle) {
    warn_unimplemented_once("bind_pass");
}
void D3D12Backend::bind_texture(int, TextureHandle) {
    warn_unimplemented_once("bind_texture");
}
void D3D12Backend::set_uniform_block(PassHandle, const void*, size_t) {
    warn_unimplemented_once("set_uniform_block");
}

void D3D12Backend::end_frame() {
    if (!ready_ || !in_frame_) return;

    D3D12_RESOURCE_BARRIER b{};
    b.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    b.Transition.pResource   = back_buffers_[current_back_buffer_].Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd_list_->ResourceBarrier(1, &b);

    cmd_list_->Close();
    ID3D12CommandList* lists[] = { cmd_list_.Get() };
    cmd_queue_->ExecuteCommandLists(1, lists);

    // Present with vsync (sync interval = 1). Tearing is left disabled in
    // Phase 3b; the flag combo for tearing on flip-discard lands in 3e
    // along with the bench config.
    swap_chain_->Present(1, 0);

    // Block until the GPU is idle. Trades ~1ms throughput for simplicity;
    // Phase 3c pipelines this with per-frame allocators+fence values.
    wait_for_gpu_idle();
    in_frame_         = false;
    default_fb_bound_ = false;
}

} // namespace tubelight

#endif // TUBELIGHT_HAVE_D3D12
