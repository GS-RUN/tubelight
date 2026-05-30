// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)

#include "render/backend_d3d12.h"

#if defined(TUBELIGHT_HAVE_D3D12)

#include <d3d11_4.h>
#include <d3d11on12.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

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
    composition_ = params.composition;
    layered_     = params.layered;

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

    // DRED (Device Removed Extended Data): auto-breadcrumbs + page-fault info,
    // always on (negligible cost). If a TDR removes the device, log_device_
    // removed() dumps the last GPU ops + faulting VA so a hang is diagnosable
    // from a user's run, not just under PIX. Must be set BEFORE device creation.
    {
        ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred)))) {
            dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
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
    if (params.enable_debug) {
        // Capture the info queue for periodic stderr drains. The debug
        // layer pushes validation messages here; without this we never
        // see them.
        device_->QueryInterface(IID_PPV_ARGS(&info_queue_));
        if (info_queue_) {
            // Documented ack (dx12-engineer): id 1424 is a benign DXGI
            // internal Present-fence message ("waiting for a fence value of
            // zero will always be satisfied") emitted on the first
            // kBackBufferCount frames of any flip-model swap chain (both the
            // ForHwnd and the Phase 4a composition path). It's a no-op wait
            // inside DXGI's Present, not Tubelight code. Filter it so real
            // validation errors stand out in the drain.
            D3D12_MESSAGE_ID deny_ids[] = {
                static_cast<D3D12_MESSAGE_ID>(1424),
            };
            D3D12_INFO_QUEUE_FILTER filter{};
            filter.DenyList.NumIDs  = _countof(deny_ids);
            filter.DenyList.pIDList = deny_ids;
            info_queue_->AddStorageFilterEntries(&filter);
        }
    }

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    if (FAILED(device_->CreateCommandQueue(&qd, IID_PPV_ARGS(&cmd_queue_)))) {
        std::fprintf(stderr, "[tubelight][d3d12] CreateCommandQueue failed\n");
        return false;
    }

    // RTV heap: backbuffers + up to kRtvHeapExtra pipeline RTs.
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
    rtv_desc.NumDescriptors = kBackBufferCount + kRtvHeapExtra;
    rtv_desc.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device_->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv_heap_)))) {
        std::fprintf(stderr, "[tubelight][d3d12] CreateDescriptorHeap (RTV) failed\n");
        return false;
    }
    rtv_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // CPU-only staging heap: where SRVs are created by create_texture /
    // create_render_target. CPU-readable, so it's a valid COPY source.
    D3D12_DESCRIPTOR_HEAP_DESC srv_cpu_desc{};
    srv_cpu_desc.NumDescriptors = kSrvHeapMax;
    srv_cpu_desc.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_cpu_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device_->CreateDescriptorHeap(&srv_cpu_desc, IID_PPV_ARGS(&srv_cpu_heap_)))) {
        std::fprintf(stderr, "[tubelight][d3d12] CreateDescriptorHeap (CPU staging) failed\n");
        return false;
    }
    // Shader-visible scratch heap: per-draw 2-SRV table assembled via
    // CopyDescriptorsSimple from srv_cpu_heap_.
    D3D12_DESCRIPTOR_HEAP_DESC srv_desc{};
    srv_desc.NumDescriptors = kSrvHeapMax;
    srv_desc.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device_->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&srv_heap_)))) {
        std::fprintf(stderr, "[tubelight][d3d12] CreateDescriptorHeap (scratch SV) failed\n");
        return false;
    }
    srv_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    if (!create_root_signature()) return false;
    if (!create_cb_ring())        return false;

    // One allocator per frame in flight (DX-22).
    for (UINT i = 0; i < kBackBufferCount; ++i) {
        if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                    IID_PPV_ARGS(&cmd_alloc_[i])))) {
            std::fprintf(stderr, "[tubelight][d3d12] CreateCommandAllocator[%u] failed\n", i);
            return false;
        }
    }
    if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           cmd_alloc_[0].Get(), nullptr,
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
    // Composition swap chains require STRETCH scaling; the HWND path keeps
    // NONE (1:1, no resampling).
    // Both composition (DComp display) and layered (ULW present) render into
    // a composition swap chain — the only flavour that is NOT bound to the
    // HWND, which CreateSwapChainForHwnd forbids on WS_EX_LAYERED windows.
    const bool comp_swapchain = composition_ || layered_;
    scd.Scaling     = comp_swapchain ? DXGI_SCALING_STRETCH : DXGI_SCALING_NONE;
    scd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode   = DXGI_ALPHA_MODE_IGNORE;
    scd.Flags       = 0;

    ComPtr<IDXGISwapChain1> sc1;
    if (comp_swapchain) {
        // Phase 4a: a swap chain NOT bound to the HWND, composited onto the
        // window through a DirectComposition visual tree. This is what lets
        // a flip-model swap chain coexist with WS_EX_LAYERED|TRANSPARENT for
        // cross-process click-through (which CreateSwapChainForHwnd forbids).
        if (FAILED(dxgi_factory_->CreateSwapChainForComposition(
                cmd_queue_.Get(), &scd, nullptr, &sc1))) {
            std::fprintf(stderr, "[tubelight][d3d12] CreateSwapChainForComposition failed\n");
            return false;
        }
        if (FAILED(sc1.As(&swap_chain_))) {
            std::fprintf(stderr, "[tubelight][d3d12] composition swap chain QI failed\n");
            return false;
        }
        if (composition_) {
            // Display via DirectComposition. The `layered_` path skips this:
            // it never shows the swap chain — the overlay reads frames back
            // and blits them onto a WS_EX_LAYERED window via UpdateLayeredWindow.
            if (FAILED(DCompositionCreateDevice(nullptr, IID_PPV_ARGS(&dcomp_device_))) ||
                FAILED(dcomp_device_->CreateTargetForHwnd(hwnd_, TRUE, &dcomp_target_)) ||
                FAILED(dcomp_device_->CreateVisual(&dcomp_visual_))) {
                std::fprintf(stderr, "[tubelight][d3d12] DirectComposition setup failed\n");
                return false;
            }
            dcomp_visual_->SetContent(swap_chain_.Get());
            dcomp_target_->SetRoot(dcomp_visual_.Get());
            if (FAILED(dcomp_device_->Commit())) {
                std::fprintf(stderr, "[tubelight][d3d12] DComp Commit failed\n");
                return false;
            }
            std::fprintf(stderr, "[tubelight][d3d12] DirectComposition visual tree ready\n");
        } else {
            std::fprintf(stderr, "[tubelight][d3d12] layered ULW present mode "
                                 "(composition swap chain, no DComp display)\n");
        }
    } else {
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

void D3D12Backend::drain_info_queue() {
    if (!info_queue_) return;
    const UINT64 n = info_queue_->GetNumStoredMessages();
    for (UINT64 i = 0; i < n; ++i) {
        SIZE_T msg_size = 0;
        info_queue_->GetMessage(i, nullptr, &msg_size);
        std::vector<uint8_t> buf(msg_size);
        auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
        if (SUCCEEDED(info_queue_->GetMessage(i, msg, &msg_size))) {
            std::fprintf(stderr, "[d3d12-debug][sev=%d id=%d] %.*s\n",
                         (int)msg->Severity, (int)msg->ID,
                         (int)msg->DescriptionByteLength, msg->pDescription);
        }
    }
    info_queue_->ClearStoredMessages();
}

void D3D12Backend::invalidate_baked_tables() {
    for (auto& kv : passes_) {
        PassEntry& pe = kv.second;
        for (UINT f = 0; f < kBackBufferCount; ++f) {
            pe.baked[f][0] = false;
            pe.baked[f][1] = false;
        }
    }
}

void D3D12Backend::log_device_removed(const char* where) {
    const unsigned long rr =
        device_ ? static_cast<unsigned long>(device_->GetDeviceRemovedReason()) : 0;
    std::fprintf(stderr, "[tubelight][d3d12] DEVICE REMOVED at %s: reason 0x%lX\n",
                 where ? where : "?", rr);
    ComPtr<ID3D12DeviceRemovedExtendedData> dred;
    if (!device_ || FAILED(device_->QueryInterface(IID_PPV_ARGS(&dred)))) return;
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT bc{};
    if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&bc))) {
        int n = 0;
        for (const D3D12_AUTO_BREADCRUMB_NODE* node = bc.pHeadAutoBreadcrumbNode;
             node && n < 8; node = node->pNext, ++n) {
            const UINT done = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0;
            std::fprintf(stderr, "  [dred] list '%ls': completed %u of %u ops\n",
                         node->pCommandListDebugNameW ? node->pCommandListDebugNameW : L"?",
                         done, node->BreadcrumbCount);
        }
    }
    D3D12_DRED_PAGE_FAULT_OUTPUT pf{};
    if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&pf)) && pf.PageFaultVA) {
        std::fprintf(stderr, "  [dred] page fault VA = 0x%llX\n",
                     static_cast<unsigned long long>(pf.PageFaultVA));
    }
}

void D3D12Backend::wait_for_fence(UINT64 value) {
    if (!fence_ || value == 0) return;
    if (fence_->GetCompletedValue() < value) {
        fence_->SetEventOnCompletion(value, fence_event_);
        WaitForSingleObject(fence_event_, INFINITE);
    }
}

void D3D12Backend::set_frame_timing(bool enabled) {
    timing_enabled_ = enabled;
    if (!enabled || ts_query_heap_ || !device_) return;
    // Lazily create a 2-slot TIMESTAMP query heap + a 16-byte READBACK
    // buffer for the resolved start/end timestamps, and cache the queue
    // tick frequency.
    D3D12_QUERY_HEAP_DESC qhd{};
    qhd.Type  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qhd.Count = 2;
    if (FAILED(device_->CreateQueryHeap(&qhd, IID_PPV_ARGS(&ts_query_heap_)))) {
        std::fprintf(stderr, "[tubelight][d3d12] CreateQueryHeap(TIMESTAMP) failed\n");
        timing_enabled_ = false;
        return;
    }
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = sizeof(UINT64) * 2;
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&ts_readback_)))) {
        std::fprintf(stderr, "[tubelight][d3d12] timestamp readback CCResource failed\n");
        ts_query_heap_.Reset();
        timing_enabled_ = false;
        return;
    }
    if (cmd_queue_) cmd_queue_->GetTimestampFrequency(&ts_frequency_);
}

void D3D12Backend::finish() {
    wait_for_gpu_idle();
    if (!timing_enabled_ || !ts_wrote_ || !ts_readback_ || ts_frequency_ == 0) return;
    // GPU work is done; read the resolved start/end timestamps.
    void* mapped = nullptr;
    D3D12_RANGE rr{ 0, sizeof(UINT64) * 2 };
    if (SUCCEEDED(ts_readback_->Map(0, &rr, &mapped)) && mapped) {
        const UINT64* ts = static_cast<const UINT64*>(mapped);
        const UINT64 delta = (ts[1] >= ts[0]) ? (ts[1] - ts[0]) : 0;
        last_gpu_ms_ = static_cast<double>(delta) * 1000.0 /
                       static_cast<double>(ts_frequency_);
        D3D12_RANGE wr{ 0, 0 };
        ts_readback_->Unmap(0, &wr);
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

// ----- helpers -----------------------------------------------------------

UINT D3D12Backend::alloc_rtv_slot() {
    if (!free_rtv_slots_.empty()) {
        UINT s = free_rtv_slots_.back();
        free_rtv_slots_.pop_back();
        return s;
    }
    if (next_rtv_slot_ >= kBackBufferCount + kRtvHeapExtra) {
        std::fprintf(stderr, "[tubelight][d3d12] RTV heap exhausted\n");
        return UINT_MAX;
    }
    return next_rtv_slot_++;
}
UINT D3D12Backend::alloc_srv_cpu_slot() {
    if (!free_srv_cpu_slots_.empty()) {
        UINT s = free_srv_cpu_slots_.back();
        free_srv_cpu_slots_.pop_back();
        return s;
    }
    if (next_srv_cpu_slot_ >= kSrvHeapMax) {
        std::fprintf(stderr, "[tubelight][d3d12] SRV CPU heap exhausted\n");
        return UINT_MAX;
    }
    return next_srv_cpu_slot_++;
}
D3D12_CPU_DESCRIPTOR_HANDLE D3D12Backend::rtv_cpu_handle(UINT slot) const {
    D3D12_CPU_DESCRIPTOR_HANDLE h = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    h.ptr += slot * rtv_descriptor_size_;
    return h;
}
D3D12_CPU_DESCRIPTOR_HANDLE D3D12Backend::srv_cpu_handle(UINT slot) const {
    D3D12_CPU_DESCRIPTOR_HANDLE h = srv_cpu_heap_->GetCPUDescriptorHandleForHeapStart();
    h.ptr += slot * srv_descriptor_size_;
    return h;
}
D3D12_CPU_DESCRIPTOR_HANDLE D3D12Backend::scratch_cpu_handle(UINT slot) const {
    D3D12_CPU_DESCRIPTOR_HANDLE h = srv_heap_->GetCPUDescriptorHandleForHeapStart();
    h.ptr += slot * srv_descriptor_size_;
    return h;
}
D3D12_GPU_DESCRIPTOR_HANDLE D3D12Backend::scratch_gpu_handle(UINT slot) const {
    D3D12_GPU_DESCRIPTOR_HANDLE h = srv_heap_->GetGPUDescriptorHandleForHeapStart();
    h.ptr += slot * srv_descriptor_size_;
    return h;
}

void D3D12Backend::transition_texture(TextureEntry& e, D3D12_RESOURCE_STATES new_state) {
    if (e.state == new_state || !e.resource) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    b.Transition.pResource   = e.resource.Get();
    b.Transition.StateBefore = e.state;
    b.Transition.StateAfter  = new_state;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd_list_->ResourceBarrier(1, &b);
    e.state = new_state;
}
void D3D12Backend::transition_rt(RenderTargetEntry& e, D3D12_RESOURCE_STATES new_state) {
    if (e.state == new_state || !e.resource) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    b.Transition.pResource   = e.resource.Get();
    b.Transition.StateBefore = e.state;
    b.Transition.StateAfter  = new_state;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd_list_->ResourceBarrier(1, &b);
    e.state = new_state;
}

bool D3D12Backend::create_root_signature() {
    // Layout (matches the GLSL convention: UBO at binding=0, samplers at
    // bindings 1 & 2; SPIRV-Cross maps them to b0, t1, t2, s1, s2):
    //   root[0] = CBV (b0)  — PassUniforms constant buffer
    //   root[1] = descriptor table [SRV t1, SRV t2]
    //   2 static samplers, both linear-clamp (s1 = u_source, s2 = secondary)
    D3D12_DESCRIPTOR_RANGE srv_range{};
    srv_range.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srv_range.NumDescriptors                    = 2;
    srv_range.BaseShaderRegister                = 1;   // t1
    srv_range.RegisterSpace                     = 0;
    srv_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER root_params[2]{};
    root_params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
    root_params[0].Descriptor.ShaderRegister = 0;
    root_params[0].Descriptor.RegisterSpace  = 0;

    root_params[1].ParameterType    = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    root_params[1].DescriptorTable.NumDescriptorRanges = 1;
    root_params[1].DescriptorTable.pDescriptorRanges   = &srv_range;

    // Both samplers LINEAR-CLAMP. GL backend uses NEAREST for the PNG
    // source texture but LINEAR for the cascade of intermediate FBOs
    // (the slot-0 binding is dynamic: first pass = source, later passes
    // = previous RT). Since the slot is reused and we can't bind two
    // samplers to the same shader register, LINEAR everywhere is the
    // closest match — the dominant cascade reads through LINEAR in GL.
    // Slight precision divergence in pass 0 vs GL is absorbed in the
    // M1 gate (perceptually invisible).
    D3D12_STATIC_SAMPLER_DESC samplers[2]{};
    for (int i = 0; i < 2; ++i) {
        samplers[i].Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplers[i].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].MipLODBias       = 0.0f;
        samplers[i].MaxAnisotropy    = 0;
        samplers[i].ComparisonFunc   = D3D12_COMPARISON_FUNC_ALWAYS;
        samplers[i].BorderColor      = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
        samplers[i].MinLOD           = 0.0f;
        samplers[i].MaxLOD           = D3D12_FLOAT32_MAX;
        samplers[i].ShaderRegister   = static_cast<UINT>(i + 1);  // s1, s2
        samplers[i].RegisterSpace    = 0;
        samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }

    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters     = 2;
    rsd.pParameters       = root_params;
    rsd.NumStaticSamplers = 2;
    rsd.pStaticSamplers   = samplers;
    rsd.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sig;
    ComPtr<ID3DBlob> err;
    HRESULT hr = D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1,
                                              &sig, &err);
    if (FAILED(hr)) {
        std::fprintf(stderr, "[tubelight][d3d12] D3D12SerializeRootSignature failed: %s\n",
                     err ? static_cast<const char*>(err->GetBufferPointer()) : "(no msg)");
        return false;
    }
    if (FAILED(device_->CreateRootSignature(0, sig->GetBufferPointer(),
                                             sig->GetBufferSize(),
                                             IID_PPV_ARGS(&root_sig_)))) {
        std::fprintf(stderr, "[tubelight][d3d12] CreateRootSignature failed\n");
        return false;
    }
    return true;
}

bool D3D12Backend::create_cb_ring() {
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type                 = D3D12_HEAP_TYPE_UPLOAD;
    hp.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    hp.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    hp.CreationNodeMask = 1; hp.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC rd{};
    rd.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Alignment          = 0;
    rd.Width              = kCbRingBytes;
    rd.Height             = 1;
    rd.DepthOrArraySize   = 1;
    rd.MipLevels          = 1;
    rd.Format             = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count   = 1;
    rd.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags              = D3D12_RESOURCE_FLAG_NONE;

    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&cb_ring_)))) {
        std::fprintf(stderr, "[tubelight][d3d12] CB ring CreateCommittedResource failed\n");
        return false;
    }
    D3D12_RANGE read_range{0, 0};
    if (FAILED(cb_ring_->Map(0, &read_range, reinterpret_cast<void**>(&cb_ring_mapped_)))) {
        std::fprintf(stderr, "[tubelight][d3d12] CB ring Map failed\n");
        return false;
    }
    cb_ring_next_ = 0;
    return true;
}
void D3D12Backend::destroy_cb_ring() {
    if (cb_ring_ && cb_ring_mapped_) {
        D3D12_RANGE empty{0, 0};
        cb_ring_->Unmap(0, &empty);
        cb_ring_mapped_ = nullptr;
    }
    cb_ring_.Reset();
}

void D3D12Backend::shutdown() {
    if (!ready_) {
        if (fence_event_) { CloseHandle(fence_event_); fence_event_ = nullptr; }
        return;
    }
    wait_for_gpu_idle();
    passes_.clear();
    textures_.clear();
    rts_.clear();
    free_rtv_slots_.clear();
    free_srv_cpu_slots_.clear();
    destroy_cb_ring();
    root_sig_.Reset();
    srv_heap_.Reset();
    srv_cpu_heap_.Reset();
    destroy_swap_chain_resources();
    // DirectComposition teardown (Phase 4a) — release before the swap chain
    // it references.
    dcomp_visual_.Reset();
    dcomp_target_.Reset();
    dcomp_device_.Reset();
    swap_chain_.Reset();
    cmd_list_.Reset();
    for (UINT i = 0; i < kBackBufferCount; ++i) cmd_alloc_[i].Reset();
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
    // ResizeBuffers requires NO outstanding references to the back buffers. A
    // closed+executed command list still holds the resources it recorded until
    // it is Reset, so reset both the main and capture lists first — otherwise
    // ResizeBuffers fails, the swap chain stays the old size while the pipeline
    // moves to the new one, and the size mismatch can fault the GPU (TDR).
    cmd_list_->Reset(cmd_alloc_[current_back_buffer_].Get(), nullptr);
    cmd_list_->Close();
    if (cap_list_ && cap_alloc_) {
        cap_list_->Reset(cap_alloc_.Get(), nullptr);
        cap_list_->Close();
    }
    destroy_swap_chain_resources();
    HRESULT hr = swap_chain_->ResizeBuffers(kBackBufferCount,
                                            static_cast<UINT>(width),
                                            static_cast<UINT>(height),
                                            kBackBufferFormat, 0);
    if (FAILED(hr)) {
        std::fprintf(stderr, "[tubelight][d3d12] swap_chain_->ResizeBuffers failed 0x%lX\n",
                     static_cast<unsigned long>(hr));
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            HRESULT rr = device_->GetDeviceRemovedReason();
            std::fprintf(stderr, "[tubelight][d3d12] device removed: 0x%lX\n",
                         static_cast<unsigned long>(rr));
        }
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
    // DX-10: do not reset this slot's allocator until the GPU has finished
    // the frame that last used it. With kBackBufferCount slots the CPU only
    // stalls here when it has lapped the GPU by that many frames — otherwise
    // this returns immediately and CPU/GPU run pipelined.
    wait_for_fence(frame_fence_value_[current_back_buffer_]);
    cmd_alloc_[current_back_buffer_]->Reset();
    cmd_list_->Reset(cmd_alloc_[current_back_buffer_].Get(), nullptr);

    // --bench: stamp GPU time at frame start (slot 0).
    ts_wrote_ = false;
    if (timing_enabled_ && ts_query_heap_) {
        cmd_list_->EndQuery(ts_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    }

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
    bound_pass_         = {0};
    bound_rt_           = {0};
    // CB ring also resets to slot 0 each frame so set_uniform_block
    // calls always land in deterministic positions.
    cb_ring_next_       = 0;
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
    // Vulkan→HLSL Y-flip trap: glslang with target_env=vulkan1.0 emits
    // SPIR-V that assumes Vulkan NDC (Y points DOWN: y=-1 is top of
    // viewport, y=+1 is bottom). SPIRV-Cross transpiles gl_Position
    // verbatim to HLSL. D3D12 NDC is Y-UP (y=+1 is top) — so without a
    // fix, the entire frame renders upside-down vs. GL.
    //
    // Standard workaround: set the viewport with negative height (top-Y
    // = y+h, height = -h). The rasterizer then flips the Y mapping,
    // making D3D behave Vulkan-style. Net result: same SPIR-V renders
    // identically in Vulkan and D3D12, no shader changes needed.
    D3D12_VIEWPORT vp{
        static_cast<FLOAT>(x),
        static_cast<FLOAT>(y + h),       // origin moves to the bottom
        static_cast<FLOAT>(w),
        -static_cast<FLOAT>(h),          // negative height flips Y
        0.0f, 1.0f
    };
    D3D12_RECT sr{ x, y, x + w, y + h };
    cmd_list_->RSSetViewports(1, &vp);
    cmd_list_->RSSetScissorRects(1, &sr);
}

void D3D12Backend::clear_color(float r, float g, float b, float a) {
    if (!ready_ || !in_frame_) return;
    // Phase 3c bug fix: previously this rebound the swap chain RTV
    // unconditionally — that breaks the pipeline cascade because Pipeline
    // binds intermediate RTs before calling clear_color. Clear whichever
    // RTV is currently bound: the swap chain backbuffer if
    // default_fb_bound_, else the cached bound_rt_'s RTV.
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    if (default_fb_bound_) {
        rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += current_back_buffer_ * rtv_descriptor_size_;
    } else if (bound_rt_.is_valid()) {
        auto it = rts_.find(bound_rt_.id);
        if (it == rts_.end()) return;
        rtv = rtv_cpu_handle(it->second.rtv_slot);
    } else {
        return;  // no target bound
    }
    const FLOAT c[4] = { r, g, b, a };
    cmd_list_->ClearRenderTargetView(rtv, c, 0, nullptr);
}

// draw_fullscreen_quad implementation moved to the F3c-4 block below
// (now drives the real PSO + descriptor table per draw).

// ----- Phase 3c F3c-4 handle API real implementation --------------------

namespace {

DXGI_FORMAT pixel_format_to_dxgi(PixelFormat fmt) {
    switch (fmt) {
        case PixelFormat::RGBA8_UNORM:  return DXGI_FORMAT_R8G8B8A8_UNORM;
        case PixelFormat::RGBA16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    }
    return DXGI_FORMAT_R8G8B8A8_UNORM;
}

std::string dxil_path(const char* name) {
#ifdef TUBELIGHT_DXIL_DIR
    return std::string(TUBELIGHT_DXIL_DIR) + "/" + name + ".dxil";
#else
    return std::string("shaders/dxil/") + name + ".dxil";
#endif
}

bool read_file_to_blob(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamsize n = f.tellg();
    if (n <= 0) return false;
    out.resize(static_cast<size_t>(n));
    f.seekg(0, std::ios::beg);
    if (!f.read(reinterpret_cast<char*>(out.data()), n)) return false;
    return true;
}

const char* kPassShaderName[8] = {
    "pass_minus1_signal",
    "pass0_analysis",
    "pass1_dither_reconstruct",
    "pass2_beam_scanlines",
    "pass3_mask",
    "pass4_bloom",
    "pass5_temporal",
    "pass6_composition",
};

} // namespace

// ----- textures ---------------------------------------------------------

TextureHandle D3D12Backend::create_texture(const TextureDesc& d) {
    if (!ready_ || d.width <= 0 || d.height <= 0) return {0};

    const DXGI_FORMAT fmt = pixel_format_to_dxgi(d.format);
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Alignment          = 0;
    rd.Width              = static_cast<UINT64>(d.width);
    rd.Height             = static_cast<UINT>(d.height);
    rd.DepthOrArraySize   = 1;
    rd.MipLevels          = 1;
    rd.Format             = fmt;
    rd.SampleDesc.Count   = 1;
    rd.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags              = D3D12_RESOURCE_FLAG_NONE;

    TextureEntry e;
    e.state  = D3D12_RESOURCE_STATE_COPY_DEST;  // upload-first lifecycle
    e.format = fmt;
    e.width  = d.width;
    e.height = d.height;
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            e.state, nullptr,
            IID_PPV_ARGS(&e.resource)))) {
        std::fprintf(stderr, "[tubelight][d3d12] create_texture: CCResource failed %dx%d\n",
                     d.width, d.height);
        return {0};
    }
    // Allocate SRV in CPU-staging heap (NOT shader-visible — see header).
    e.srv_cpu_slot = alloc_srv_cpu_slot();
    if (e.srv_cpu_slot == UINT_MAX) return {0};
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format                        = fmt;
    srv.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels           = 1;
    srv.Texture2D.MostDetailedMip     = 0;
    srv.Texture2D.ResourceMinLODClamp = 0.0f;
    device_->CreateShaderResourceView(e.resource.Get(), &srv, srv_cpu_handle(e.srv_cpu_slot));

    const uint32_t id = next_id_++;
    textures_.emplace(id, std::move(e));
    return TextureHandle{id};
}

void D3D12Backend::destroy_texture(TextureHandle h) {
    auto it = textures_.find(h.id);
    if (it == textures_.end()) return;
    // Return the CPU-SRV slot for reuse — BUT NOT for a borrowed alias of an
    // RT (rt_as_texture): that slot belongs to the RT and is freed when the RT
    // is destroyed; freeing it here too would double-free it into the list.
    if (!it->second.borrowed_from_rt && it->second.srv_cpu_slot != UINT_MAX) {
        free_srv_cpu_slots_.push_back(it->second.srv_cpu_slot);
        invalidate_baked_tables();  // a reused CPU-SRV slot must not be a false cache hit
    }
    textures_.erase(it);
}

bool D3D12Backend::upload_texture_rgba8(TextureHandle h, const void* data,
                                        int width, int height) {
    auto it = textures_.find(h.id);
    if (it == textures_.end() || !data) return false;
    if (it->second.format != DXGI_FORMAT_R8G8B8A8_UNORM) {
        std::fprintf(stderr, "[tubelight][d3d12] upload_texture_rgba8: format mismatch\n");
        return false;
    }
    if (it->second.width != width || it->second.height != height) {
        std::fprintf(stderr, "[tubelight][d3d12] upload_texture_rgba8: size mismatch %dx%d vs %dx%d\n",
                     width, height, it->second.width, it->second.height);
        return false;
    }
    auto& tex = it->second;

    // Staging UPLOAD buffer with the row-pitch alignment D3D12 demands
    // (256 B). Layout queried via GetCopyableFootprints.
    D3D12_RESOURCE_DESC dst_desc = tex.resource->GetDesc();
    UINT64 total_bytes = 0;
    UINT   num_rows = 0;
    UINT64 row_size = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    device_->GetCopyableFootprints(&dst_desc, 0, 1, 0,
                                    &layout, &num_rows, &row_size, &total_bytes);

    D3D12_HEAP_PROPERTIES up_hp{};
    up_hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC up_rd{};
    up_rd.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    up_rd.Width              = total_bytes;
    up_rd.Height             = 1;
    up_rd.DepthOrArraySize   = 1;
    up_rd.MipLevels          = 1;
    up_rd.Format             = DXGI_FORMAT_UNKNOWN;
    up_rd.SampleDesc.Count   = 1;
    up_rd.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> staging;
    if (FAILED(device_->CreateCommittedResource(
            &up_hp, D3D12_HEAP_FLAG_NONE, &up_rd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&staging)))) {
        std::fprintf(stderr, "[tubelight][d3d12] upload staging CCResource failed\n");
        return false;
    }
    void* mapped = nullptr;
    D3D12_RANGE rd_range{0, 0};
    if (FAILED(staging->Map(0, &rd_range, &mapped))) return false;
    // Copy row by row to respect the padded RowPitch.
    const uint8_t* src = static_cast<const uint8_t*>(data);
    uint8_t* dst       = static_cast<uint8_t*>(mapped) + layout.Offset;
    const UINT src_row = static_cast<UINT>(width) * 4u;
    for (UINT y = 0; y < num_rows; ++y) {
        std::memcpy(dst + static_cast<size_t>(y) * layout.Footprint.RowPitch,
                    src + static_cast<size_t>(y) * src_row,
                    src_row);
    }
    staging->Unmap(0, nullptr);

    // Upload happens on the next frame's command list. We need a
    // dedicated allocator+list pair OR we can do it synchronously here
    // by opening a private command list, executing, and waiting. Going
    // synchronous (simpler; upload only happens at load time).
    ComPtr<ID3D12CommandAllocator> up_alloc;
    ComPtr<ID3D12GraphicsCommandList> up_list;
    if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&up_alloc))) ||
        FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                          up_alloc.Get(), nullptr, IID_PPV_ARGS(&up_list)))) {
        std::fprintf(stderr, "[tubelight][d3d12] upload command list create failed\n");
        return false;
    }

    D3D12_TEXTURE_COPY_LOCATION src_loc{};
    src_loc.pResource       = staging.Get();
    src_loc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src_loc.PlacedFootprint = layout;

    D3D12_TEXTURE_COPY_LOCATION dst_loc{};
    dst_loc.pResource        = tex.resource.Get();
    dst_loc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_loc.SubresourceIndex = 0;

    up_list->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = tex.resource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    up_list->ResourceBarrier(1, &barrier);
    up_list->Close();

    ID3D12CommandList* lists[] = { up_list.Get() };
    cmd_queue_->ExecuteCommandLists(1, lists);
    wait_for_gpu_idle();  // small synchronous wait; OK at load time

    tex.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    return true;
}

// ----- render targets ---------------------------------------------------

RenderTargetHandle D3D12Backend::create_render_target(int w, int h, PixelFormat fmt) {
    if (!ready_ || w <= 0 || h <= 0) return {0};
    const DXGI_FORMAT dxgi_fmt = pixel_format_to_dxgi(fmt);

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width              = static_cast<UINT64>(w);
    rd.Height             = static_cast<UINT>(h);
    rd.DepthOrArraySize   = 1;
    rd.MipLevels          = 1;
    rd.Format             = dxgi_fmt;
    rd.SampleDesc.Count   = 1;
    rd.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags              = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clear{};
    clear.Format   = dxgi_fmt;
    clear.Color[0] = 0.0f; clear.Color[1] = 0.0f; clear.Color[2] = 0.0f; clear.Color[3] = 1.0f;

    RenderTargetEntry e;
    e.state  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    e.format = dxgi_fmt;
    e.width  = w;
    e.height = h;
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            e.state, &clear,
            IID_PPV_ARGS(&e.resource)))) {
        std::fprintf(stderr, "[tubelight][d3d12] create_render_target CCResource failed %dx%d\n", w, h);
        return {0};
    }
    e.rtv_slot     = alloc_rtv_slot();
    e.srv_cpu_slot = alloc_srv_cpu_slot();
    if (e.rtv_slot == UINT_MAX || e.srv_cpu_slot == UINT_MAX) return {0};
    device_->CreateRenderTargetView(e.resource.Get(), nullptr, rtv_cpu_handle(e.rtv_slot));
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format                        = dxgi_fmt;
    srv.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels           = 1;
    srv.Texture2D.MostDetailedMip     = 0;
    srv.Texture2D.ResourceMinLODClamp = 0.0f;
    device_->CreateShaderResourceView(e.resource.Get(), &srv, srv_cpu_handle(e.srv_cpu_slot));

    const uint32_t id = next_id_++;
    rts_.emplace(id, std::move(e));
    return RenderTargetHandle{id};
}

void D3D12Backend::destroy_render_target(RenderTargetHandle h) {
    auto it = rts_.find(h.id);
    if (it == rts_.end()) return;
    // An RT owns both its RTV slot and its CPU-SRV slot — return them so the
    // next create_render_target reuses them (prevents the resize heap leak).
    if (it->second.rtv_slot     != UINT_MAX) free_rtv_slots_.push_back(it->second.rtv_slot);
    if (it->second.srv_cpu_slot != UINT_MAX) free_srv_cpu_slots_.push_back(it->second.srv_cpu_slot);
    rts_.erase(it);
    invalidate_baked_tables();  // a reused CPU-SRV slot must not be a false cache hit
}

bool D3D12Backend::capture_backbuffer(std::vector<uint8_t>& out_rgba,
                                       int& out_width, int& out_height) {
    if (!ready_) return false;
    // After end_frame(), current_back_buffer_ still holds the index of
    // the buffer we just rendered + Present'd — that buffer is now the
    // visible front (PRESENT state). Reading the OTHER index would
    // return discarded content (FLIP_DISCARD swap effect). begin_frame
    // refreshes current_back_buffer_ via GetCurrentBackBufferIndex()
    // before drawing the next frame.
    const UINT bb_idx = current_back_buffer_;
    ID3D12Resource* src = back_buffers_[bb_idx].Get();
    if (!src) return false;
    D3D12_RESOURCE_DESC sd = src->GetDesc();
    const int w = static_cast<int>(sd.Width);
    const int h = static_cast<int>(sd.Height);

    // (Re)create the PERSISTENT readback buffer + copy allocator/list only when
    // the backbuffer size changes — never per frame. The previous capture copy
    // is always waited on before we return (below), and resize() flushes the
    // GPU, so releasing/recreating here is safe.
    if (!cap_readback_ || w != cap_w_ || h != cap_h_) {
        UINT64 row_size = 0;
        device_->GetCopyableFootprints(&sd, 0, 1, 0, &cap_layout_,
                                       &cap_num_rows_, &row_size, &cap_total_bytes_);
        D3D12_HEAP_PROPERTIES rb_hp{};
        rb_hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rb_rd{};
        rb_rd.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
        rb_rd.Width              = cap_total_bytes_;
        rb_rd.Height             = 1;
        rb_rd.DepthOrArraySize   = 1;
        rb_rd.MipLevels          = 1;
        rb_rd.Format             = DXGI_FORMAT_UNKNOWN;
        rb_rd.SampleDesc.Count   = 1;
        rb_rd.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        cap_readback_.Reset();
        if (FAILED(device_->CreateCommittedResource(
                &rb_hp, D3D12_HEAP_FLAG_NONE, &rb_rd,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                IID_PPV_ARGS(&cap_readback_)))) {
            std::fprintf(stderr, "[tubelight][d3d12] capture readback CCResource failed\n");
            return false;
        }
        if (!cap_alloc_ &&
            FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   IID_PPV_ARGS(&cap_alloc_)))) {
            std::fprintf(stderr, "[tubelight][d3d12] capture alloc create failed\n");
            return false;
        }
        if (!cap_list_) {
            if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  cap_alloc_.Get(), nullptr,
                                                  IID_PPV_ARGS(&cap_list_)))) {
                std::fprintf(stderr, "[tubelight][d3d12] capture list create failed\n");
                return false;
            }
            cap_list_->Close();  // created recording; close so the per-call Reset is uniform
        }
        cap_w_ = w; cap_h_ = h;
    }

    // Record the copy on the persistent list. The previous copy is guaranteed
    // complete (we wait on its fence every call), so resetting now is safe (DX-10).
    cap_alloc_->Reset();
    cap_list_->Reset(cap_alloc_.Get(), nullptr);

    // Backbuffer is in PRESENT state right after Present(). Transition
    // to COPY_SOURCE, copy, then transition back to PRESENT.
    D3D12_RESOURCE_BARRIER b{};
    b.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = src;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cap_list_->ResourceBarrier(1, &b);

    D3D12_TEXTURE_COPY_LOCATION sloc{};
    sloc.pResource        = src;
    sloc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    sloc.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION dloc{};
    dloc.pResource       = cap_readback_.Get();
    dloc.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dloc.PlacedFootprint = cap_layout_;
    cap_list_->CopyTextureRegion(&dloc, 0, 0, 0, &sloc, nullptr);

    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
    cap_list_->ResourceBarrier(1, &b);
    cap_list_->Close();
    ID3D12CommandList* lists[] = { cap_list_.Get() };
    cmd_queue_->ExecuteCommandLists(1, lists);

    // Wait only for THIS copy (a ~few-MB GPU copy), not a full pipeline flush:
    // signal a monotonic fence value and block on it. fence_value_ stays
    // monotonic so it never corrupts begin_frame's per-slot pacing waits.
    const UINT64 v = ++fence_value_;
    cmd_queue_->Signal(fence_.Get(), v);
    cap_fence_value_ = v;
    if (fence_->GetCompletedValue() < v) {
        fence_->SetEventOnCompletion(v, fence_event_);
        WaitForSingleObject(fence_event_, INFINITE);
    }

    // Map and copy out, stripping row padding.
    void* mapped = nullptr;
    D3D12_RANGE read_range{ 0, static_cast<SIZE_T>(cap_total_bytes_) };
    if (FAILED(cap_readback_->Map(0, &read_range, &mapped))) {
        std::fprintf(stderr, "[tubelight][d3d12] capture readback Map failed\n");
        return false;
    }
    out_width  = w;
    out_height = h;
    out_rgba.assign(static_cast<size_t>(out_width) * out_height * 4, 0);
    const uint8_t* msrc = static_cast<const uint8_t*>(mapped) + cap_layout_.Offset;
    const UINT dst_row = static_cast<UINT>(out_width) * 4u;
    for (UINT y = 0; y < cap_num_rows_; ++y) {
        std::memcpy(out_rgba.data() + static_cast<size_t>(y) * dst_row,
                    msrc + static_cast<size_t>(y) * cap_layout_.Footprint.RowPitch,
                    dst_row);
    }
    D3D12_RANGE empty{0, 0};
    cap_readback_->Unmap(0, &empty);
    return true;
}

// ----- Phase 3d: D3D11On12 + WGC interop --------------------------------

ID3D11Device* D3D12Backend::d3d11_on12_device() {
    if (!d3d11on12_init_attempted_) {
        d3d11on12_init_attempted_ = true;
        if (!device_ || !cmd_queue_) return nullptr;

        // Use D3D11 feature level matching the device feature level the
        // D3D12 device was created at — minimum 11_0 (the lowest D3D12
        // can wrap). We accept the highest D3D11 supports.
        const D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
        };
        IUnknown* queues[] = { cmd_queue_.Get() };
        // D3D11_CREATE_DEVICE_BGRA_SUPPORT lets us share with WGC
        // (which prefers BGRA8 framebuffers via DXGI_FORMAT_B8G8R8A8_UNORM).
        // D3D11_CREATE_DEVICE_DEBUG: we mirror the D3D12 debug-layer
        // toggle; cheap to ask, only takes effect with Graphics Tools
        // installed.
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        if (info_queue_) flags |= D3D11_CREATE_DEVICE_DEBUG;

        HRESULT hr = D3D11On12CreateDevice(
            device_.Get(), flags,
            levels, _countof(levels),
            queues, _countof(queues),
            0,
            d3d11_.GetAddressOf(),
            d3d11_ctx_.GetAddressOf(),
            nullptr);
        if (FAILED(hr)) {
            std::fprintf(stderr,
                "[tubelight][d3d12] D3D11On12CreateDevice failed 0x%lX\n", hr);
            return nullptr;
        }
        if (FAILED(d3d11_.As(&d3d11on12_))) {
            std::fprintf(stderr,
                "[tubelight][d3d12] ID3D11On12Device2 QI failed "
                "(requires Win10 1809+)\n");
            d3d11_.Reset();
            d3d11_ctx_.Reset();
            return nullptr;
        }
        d3d11on12_init_ok_ = true;
        std::fprintf(stderr,
            "[tubelight][d3d12] D3D11On12 device initialised (for WGC)\n");
    }
    return d3d11on12_init_ok_ ? d3d11_.Get() : nullptr;
}

TextureHandle D3D12Backend::wrap_d3d11_texture(ID3D11Texture2D* tex,
                                                int width, int height) {
    if (!d3d11on12_init_ok_ || !tex) return {0};
    void* key = static_cast<void*>(tex);
    auto cache_it = wrapped_d3d11_handles_.find(key);
    if (cache_it != wrapped_d3d11_handles_.end()) {
        // Already wrapped this exact texture pointer. WGC recycles its
        // pool of 2 textures, so the cache stays tiny (2 entries at
        // steady state). Return the cached handle.
        return cache_it->second;
    }

    // Unwrap the underlying D3D12 resource via D3D11On12. The wrapped
    // resource enters RESOURCE_STATE_PIXEL_SHADER_RESOURCE so the
    // pipeline can sample without an extra transition. After the GPU
    // is done with the frame, we ReturnUnderlyingResource (deferred
    // to end_frame for batching; for now we leave it acquired until
    // backend shutdown since each WGC texture is reused frame-to-frame).
    ComPtr<ID3D12Resource> d3d12_res;
    HRESULT hr = d3d11on12_->UnwrapUnderlyingResource(
        tex, cmd_queue_.Get(), IID_PPV_ARGS(&d3d12_res));
    if (FAILED(hr)) {
        std::fprintf(stderr,
            "[tubelight][d3d12] UnwrapUnderlyingResource 0x%lX\n", hr);
        return {0};
    }
    D3D12_RESOURCE_DESC rd = d3d12_res->GetDesc();

    // Build a TextureEntry that owns the unwrapped D3D12 resource +
    // a fresh SRV in srv_cpu_heap_. The state is PIXEL_SHADER_RESOURCE
    // (post-unwrap convention).
    TextureEntry e;
    e.resource     = d3d12_res;
    e.state        = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    e.format       = rd.Format;
    e.width        = width  > 0 ? width  : static_cast<int>(rd.Width);
    e.height       = height > 0 ? height : static_cast<int>(rd.Height);
    e.borrowed_from_rt = false;

    e.srv_cpu_slot = alloc_srv_cpu_slot();
    if (e.srv_cpu_slot == UINT_MAX) return {0};

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format                        = rd.Format;
    srv.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels           = 1;
    srv.Texture2D.MostDetailedMip     = 0;
    srv.Texture2D.ResourceMinLODClamp = 0.0f;
    device_->CreateShaderResourceView(e.resource.Get(), &srv, srv_cpu_handle(e.srv_cpu_slot));

    const uint32_t id = next_id_++;
    textures_.emplace(id, std::move(e));
    TextureHandle h{id};
    wrapped_d3d11_handles_.emplace(key, h);
    return h;
}

TextureHandle D3D12Backend::rt_as_texture(RenderTargetHandle h) {
    auto it = rts_.find(h.id);
    if (it == rts_.end()) return {0};
    // Build a TextureEntry that aliases the RT's resource + SRV. The
    // alias is borrowed; destroy_texture won't free the underlying
    // resource (the RT owns it).
    TextureEntry e;
    e.resource         = it->second.resource;  // shared ComPtr
    e.state            = it->second.state;      // tracked separately, see below
    e.srv_cpu_slot     = it->second.srv_cpu_slot;   // alias the CPU SRV
    e.format           = it->second.format;
    e.width            = it->second.width;
    e.height           = it->second.height;
    e.borrowed_from_rt = true;
    e.source_rt        = h;
    const uint32_t id = next_id_++;
    textures_.emplace(id, std::move(e));
    return TextureHandle{id};
}

// ----- passes -----------------------------------------------------------

PassHandle D3D12Backend::create_pass(const PassDesc& d) {
    if (!ready_ || d.pass_index < 0 || d.pass_index >= 8) return {0};

    std::vector<uint8_t> vs_blob, fs_blob;
    if (!read_file_to_blob(dxil_path("fullscreen"), vs_blob) ||
        !read_file_to_blob(dxil_path(kPassShaderName[d.pass_index]), fs_blob)) {
        std::fprintf(stderr, "[tubelight][d3d12] create_pass: DXIL not found for pass %d\n",
                     d.pass_index);
        return {0};
    }

    // PSO base description — intermediate format (R16G16B16A16_FLOAT).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature       = root_sig_.Get();
    pso.VS.pShaderBytecode   = vs_blob.data();
    pso.VS.BytecodeLength    = vs_blob.size();
    pso.PS.pShaderBytecode   = fs_blob.data();
    pso.PS.BytecodeLength    = fs_blob.size();
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.BlendState.RenderTarget[0].BlendEnable           = FALSE;
    pso.BlendState.RenderTarget[0].LogicOpEnable         = FALSE;
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.DepthStencilState.DepthEnable   = FALSE;
    pso.DepthStencilState.StencilEnable = FALSE;
    pso.SampleMask           = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets     = 1;
    pso.SampleDesc.Count     = 1;

    PassEntry e;
    e.pass_index          = d.pass_index;
    e.uniform_block_bytes = d.uniform_block_bytes;

    // Reserve a persistent 2-SRV table per frame in flight in the
    // shader-visible heap (double-buffered → no cross-frame overwrite).
    for (UINT f = 0; f < kBackBufferCount; ++f) {
        if (next_srv_gpu_slot_ + 2 > kSrvHeapMax) {
            std::fprintf(stderr, "[tubelight][d3d12] srv_heap_ exhausted for pass %d\n",
                         d.pass_index);
            return {0};
        }
        e.gpu_table_slot[f] = next_srv_gpu_slot_;
        next_srv_gpu_slot_ += 2;
    }

    pso.RTVFormats[0] = kIntermediateFormat;
    if (FAILED(device_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&e.pso_intermediate)))) {
        std::fprintf(stderr, "[tubelight][d3d12] CreateGraphicsPipelineState (intermediate) failed for pass %d\n",
                     d.pass_index);
        return {0};
    }
    pso.RTVFormats[0] = kBackBufferFormat;
    if (FAILED(device_->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&e.pso_backbuffer)))) {
        std::fprintf(stderr, "[tubelight][d3d12] CreateGraphicsPipelineState (backbuffer) failed for pass %d\n",
                     d.pass_index);
        return {0};
    }

    const uint32_t id = next_id_++;
    passes_.emplace(id, std::move(e));
    return PassHandle{id};
}

void D3D12Backend::destroy_pass(PassHandle h) {
    passes_.erase(h.id);
    if (bound_pass_.id == h.id) bound_pass_ = {0};
}

// ----- copy -------------------------------------------------------------

void D3D12Backend::copy_rt_to_texture(RenderTargetHandle src, TextureHandle dst) {
    auto sit = rts_.find(src.id);
    auto dit = textures_.find(dst.id);
    if (sit == rts_.end() || dit == textures_.end() || !in_frame_) return;
    transition_rt(sit->second, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition_texture(dit->second, D3D12_RESOURCE_STATE_COPY_DEST);

    D3D12_TEXTURE_COPY_LOCATION sloc{};
    sloc.pResource        = sit->second.resource.Get();
    sloc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    sloc.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION dloc{};
    dloc.pResource        = dit->second.resource.Get();
    dloc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dloc.SubresourceIndex = 0;
    cmd_list_->CopyTextureRegion(&dloc, 0, 0, 0, &sloc, nullptr);

    transition_texture(dit->second, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    transition_rt(sit->second, D3D12_RESOURCE_STATE_RENDER_TARGET);
}

// ----- bind / draw ------------------------------------------------------

void D3D12Backend::bind_render_target(RenderTargetHandle h) {
    if (!ready_ || !in_frame_) return;
    if (!h.is_valid()) {
        // Default framebuffer (current backbuffer).
        bind_default_framebuffer();
        bound_rt_ = {0};
        return;
    }
    auto it = rts_.find(h.id);
    if (it == rts_.end()) return;
    transition_rt(it->second, D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_cpu_handle(it->second.rtv_slot);
    cmd_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    bound_rt_ = h;
    default_fb_bound_ = false;
}

void D3D12Backend::bind_pass(PassHandle h) {
    auto it = passes_.find(h.id);
    if (it == passes_.end() || !in_frame_) return;
    // Pick PSO based on the currently-bound target's format.
    const bool to_bb = default_fb_bound_;
    ID3D12PipelineState* pso = to_bb
        ? it->second.pso_backbuffer.Get()
        : it->second.pso_intermediate.Get();
    cmd_list_->SetPipelineState(pso);
    cmd_list_->SetGraphicsRootSignature(root_sig_.Get());
    ID3D12DescriptorHeap* heaps[] = { srv_heap_.Get() };
    cmd_list_->SetDescriptorHeaps(1, heaps);
    cmd_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // Reset per-pass texture bindings; bind_texture fills them in before draw.
    it->second.slot_set[0] = it->second.slot_set[1] = false;
    bound_pass_ = h;
}

void D3D12Backend::bind_texture(int slot, TextureHandle h) {
    auto pit = passes_.find(bound_pass_.id);
    auto tit = textures_.find(h.id);
    if (pit == passes_.end() || tit == textures_.end()) return;
    if (slot < 0 || slot >= 2) return;
    // If the texture aliases an RT, make sure the underlying RT is in
    // PIXEL_SHADER_RESOURCE state — cascade in Pipeline binds the
    // previous pass's output RT as input.
    if (tit->second.borrowed_from_rt) {
        auto rit = rts_.find(tit->second.source_rt.id);
        if (rit != rts_.end()) {
            transition_rt(rit->second, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
    } else {
        transition_texture(tit->second, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    pit->second.slot_cpu[slot] = srv_cpu_handle(tit->second.srv_cpu_slot);
    pit->second.slot_set[slot] = true;
}

void D3D12Backend::set_uniform_block(PassHandle h, const void* data, size_t bytes) {
    auto it = passes_.find(h.id);
    if (it == passes_.end() || !data) return;
    assert(bytes == it->second.uniform_block_bytes);
    if (bytes > kCbSlotBytes) {
        std::fprintf(stderr, "[tubelight][d3d12] set_uniform_block: %zu > kCbSlotBytes %u\n",
                     bytes, kCbSlotBytes);
        return;
    }
    // Grab the next ring slot. Wraps when full — we wait_for_gpu_idle()
    // at the end of every frame so this can't race the GPU.
    const UINT slot = cb_ring_next_;
    cb_ring_next_ = (cb_ring_next_ + 1) % kCbRingSlots;
    std::memcpy(cb_ring_mapped_ + slot * kCbSlotBytes, data, bytes);
    const D3D12_GPU_VIRTUAL_ADDRESS gpu =
        cb_ring_->GetGPUVirtualAddress() + slot * kCbSlotBytes;
    it->second.last_cbv = gpu;
}

void D3D12Backend::draw_fullscreen_quad() {
    if (!ready_ || !in_frame_ || !bound_pass_.is_valid()) return;
    auto pit = passes_.find(bound_pass_.id);
    if (pit == passes_.end()) return;

    PassEntry& pe = pit->second;

    // Apply root parameters captured by set_uniform_block + bind_texture.
    if (pe.last_cbv) {
        cmd_list_->SetGraphicsRootConstantBufferView(0, pe.last_cbv);
    }

    // Resolve the desired source SRV for each of the 2 table slots. Slot 1
    // falls back to slot 0's texture when unset (the shader gates sampling
    // via u_has_bezel_image / u_history_valid). A pass with no texture at
    // all binds nothing (skip the table).
    const D3D12_CPU_DESCRIPTOR_HANDLE null_handle{0};
    D3D12_CPU_DESCRIPTOR_HANDLE want[2] = { null_handle, null_handle };
    if (pe.slot_set[0]) want[0] = pe.slot_cpu[0];
    if (pe.slot_set[1]) want[1] = pe.slot_cpu[1];
    else if (pe.slot_set[0]) want[1] = pe.slot_cpu[0];

    if (want[0].ptr != 0) {
        // Use this frame-in-flight's persistent table (double-buffered, so
        // baking it can't race the GPU still reading the other frame's).
        const UINT f         = current_back_buffer_;
        const UINT base_slot = pe.gpu_table_slot[f];

        // Re-copy into the shader-visible table ONLY when a binding changed
        // vs. what was last baked for this (pass, frame) — steady state is
        // zero copies. This replaces the old per-draw CopyDescriptorsSimple.
        for (int s = 0; s < 2; ++s) {
            if (!pe.baked[f][s] || pe.baked_cpu[f][s].ptr != want[s].ptr) {
                device_->CopyDescriptorsSimple(
                    1, scratch_cpu_handle(base_slot + s), want[s],
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                pe.baked_cpu[f][s] = want[s];
                pe.baked[f][s]     = true;
            }
        }
        cmd_list_->SetGraphicsRootDescriptorTable(1, scratch_gpu_handle(base_slot));
    }

    // Three-vertex fullscreen triangle — VS uses SV_VertexID to generate
    // positions; no vertex buffer needed.
    cmd_list_->DrawInstanced(3, 1, 0, 0);
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

    // --bench: stamp GPU time at frame end (slot 1) and resolve both into
    // the READBACK buffer; finish() reads them once the fence passes.
    if (timing_enabled_ && ts_query_heap_ && ts_readback_) {
        cmd_list_->EndQuery(ts_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        cmd_list_->ResolveQueryData(ts_query_heap_.Get(),
                                    D3D12_QUERY_TYPE_TIMESTAMP, 0, 2,
                                    ts_readback_.Get(), 0);
        ts_wrote_ = true;
    }

    cmd_list_->Close();
    ID3D12CommandList* lists[] = { cmd_list_.Get() };
    cmd_queue_->ExecuteCommandLists(1, lists);

    // Present with vsync (sync interval = 1). Tearing is left disabled in
    // Phase 3b; the flag combo for tearing on flip-discard lands in 3e
    // along with the bench config.
    //
    // EXCEPTION — --bench: when frame timing is on we skip Present. A
    // vsync'd Present throttles the GPU queue and pollutes the timestamp
    // window (the EndQuery..EndQuery delta then captures queue-wait time,
    // not pipeline work), which inflated the early Phase 3e DX12 numbers.
    // Skipping it isolates the pure pipeline GPU cost. (current_back_buffer_
    // stays put without Present → the bench runs 1-frame-in-flight, which
    // is exactly what we want for an isolated per-frame measurement.)
    if (!timing_enabled_ && present_enabled_) {
        HRESULT pr = swap_chain_->Present(vsync_ ? 1 : 0, 0);
        if (pr == DXGI_ERROR_DEVICE_REMOVED || pr == DXGI_ERROR_DEVICE_RESET)
            log_device_removed("Present");
    }

    // Frame pacing (DX-22 + DX-10): signal a fence value for this slot and
    // record it, but do NOT block. begin_frame waits on this value only
    // when it next recycles this slot (kBackBufferCount frames later), so
    // the CPU stays up to kBackBufferCount-1 frames ahead of the GPU
    // instead of stalling on every Present.
    const UINT64 signaled = ++fence_value_;
    cmd_queue_->Signal(fence_.Get(), signaled);
    frame_fence_value_[current_back_buffer_] = signaled;

    in_frame_         = false;
    default_fb_bound_ = false;

    // Drain debug-layer messages once per frame so validation errors
    // hit stderr (no debugger required).
    drain_info_queue();
}

} // namespace tubelight

#endif // TUBELIGHT_HAVE_D3D12
