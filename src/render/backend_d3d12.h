// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)
//
// Phase 3b of ADR-0002 — D3D12 backend skeleton.
//
// SCOPE: this implementation can create a D3D12 device + DXGI flip-discard
// swap chain, clear the backbuffer, and Present. It DOES NOT yet drive the
// 8-pass CRT Pipeline — that requires the abstract resource handles +
// HLSL pass ports landing in Phase 3c. `supports_pipeline()` returns false.
//
// Why ship a non-driving backend? Two reasons:
//  1. Validates the windowing + adapter selection + swap chain plumbing
//     end-to-end on real hardware before we sink a week into HLSL ports.
//  2. Exposes the `--renderer dx12` CLI flag so users / CI can smoke-test
//     D3D12 device creation on their hardware (R12 mitigation).
//
// Synchronisation in Phase 3b is intentionally simple: one command
// allocator, one command list, one fence — wait for GPU idle on every
// Present. ~1 ms penalty vs. a proper triple-buffered pipeline; acceptable
// for a skeleton that only clears the screen. Pipelining lands in 3c.

#pragma once

#include "render/backend.h"

#if defined(TUBELIGHT_HAVE_D3D12)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

// D3D11On12 for WGC interop (Phase 3d). The 11On12 device wraps a
// D3D11 view on top of our D3D12 device so WGC (which is D3D11-only)
// can hand us textures that we can then unwrap back as D3D12 resources
// without copying. ID3D11On12Device2 (Win10 1809+) provides the
// Unwrap/ReturnUnderlyingResource pair we use.
#include <d3d11_4.h>
#include <d3d11on12.h>

namespace tubelight {

class D3D12Backend final : public IRenderBackend {
public:
    D3D12Backend() = default;
    ~D3D12Backend() override;

    D3D12Backend(const D3D12Backend&) = delete;
    D3D12Backend& operator=(const D3D12Backend&) = delete;

    bool init(const BackendInitParams& params) override;
    void shutdown() override;
    void resize(int width, int height) override;
    const char* name() const override { return name_buf_; }

    void begin_frame() override;
    void bind_default_framebuffer() override;
    void set_viewport(int x, int y, int w, int h) override;
    void clear_color(float r, float g, float b, float a) override;
    void draw_fullscreen_quad() override;
    void end_frame() override;

    // Phase 3c F3c-4: D3D12 backend now drives the 8-pass Pipeline.
    bool supports_pipeline() const override { return true; }

    // ----- Phase 3c handle API (F3c-2: stubs; F3c-4: real impl) ------
    TextureHandle      create_texture(const TextureDesc&) override;
    RenderTargetHandle create_render_target(int w, int h, PixelFormat) override;
    PassHandle         create_pass(const PassDesc&) override;

    void destroy_texture(TextureHandle) override;
    void destroy_render_target(RenderTargetHandle) override;
    void destroy_pass(PassHandle) override;

    bool upload_texture_rgba8(TextureHandle, const void*, int, int) override;
    void copy_rt_to_texture(RenderTargetHandle, TextureHandle) override;

    void bind_render_target(RenderTargetHandle) override;
    void bind_pass(PassHandle) override;
    void bind_texture(int slot, TextureHandle) override;
    void set_uniform_block(PassHandle, const void* data, size_t bytes) override;
    TextureHandle rt_as_texture(RenderTargetHandle) override;
    bool capture_backbuffer(std::vector<uint8_t>& out_rgba,
                             int& out_width, int& out_height) override;

    // ----- Phase 3d D3D11On12 + WGC interop --------------------------
    // Lazily create a D3D11 device that wraps our D3D12 device + queue
    // via D3D11On12CreateDevice. WGC (Windows.Graphics.Capture) takes a
    // D3D11 device, so this lets us share resources with it without an
    // extra cross-device copy.
    ID3D11Device* d3d11_on12_device();

    // Wrap a WGC-delivered ID3D11Texture2D as a TextureHandle that the
    // pipeline can sample. Internally:
    //   - First call for a given d3d11 texture pointer: unwraps the
    //     underlying ID3D12Resource via ID3D11On12Device::
    //     UnwrapUnderlyingResource, creates an SRV in srv_cpu_heap_,
    //     stashes both in wrapped_d3d11_.
    //   - Subsequent calls: returns the cached handle (same SRV slot).
    // Caller MUST hold the D3D11 texture alive for the handle lifetime.
    TextureHandle wrap_d3d11_texture(ID3D11Texture2D* tex,
                                      int width, int height);

private:
    static constexpr UINT kBackBufferCount = 2;
    static constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    static constexpr DXGI_FORMAT kIntermediateFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    // CB ring sized for 32 set_uniform_block calls per frame (8 passes
    // × 4-frame safety margin). Each slot is 256 B (CB alignment minimum
    // on D3D12, max PassUniforms size is 80 B so fits comfortably).
    static constexpr UINT kCbSlotBytes  = 256;
    static constexpr UINT kCbRingSlots  = 64;
    static constexpr UINT kCbRingBytes  = kCbSlotBytes * kCbRingSlots;
    // CBV/SRV/UAV heap: one CBV slot per ring slot + N SRVs for textures/RTs.
    static constexpr UINT kSrvHeapMax   = 256;

    void wait_for_gpu_idle();
    bool create_swap_chain_resources(int width, int height);
    void destroy_swap_chain_resources();
    bool create_root_signature();
    bool create_cb_ring();
    void destroy_cb_ring();
    void drain_info_queue();
    Microsoft::WRL::ComPtr<ID3D12InfoQueue>         info_queue_;

    // D3D11On12 + WGC interop state (Phase 3d). Lazily initialised by
    // d3d11_on12_device(); never freed until shutdown.
    Microsoft::WRL::ComPtr<ID3D11Device>            d3d11_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>     d3d11_ctx_;
    Microsoft::WRL::ComPtr<ID3D11On12Device2>       d3d11on12_;
    bool d3d11on12_init_attempted_ = false;
    bool d3d11on12_init_ok_ = false;

    // Cache of wrapped D3D11 textures: ID3D11Texture2D* → TextureHandle.
    // Lookup is by pointer (WGC recycles texture pointers across frames
    // when buffer count is reached, so the cache stays small).
    std::unordered_map<void*, TextureHandle> wrapped_d3d11_handles_;

    // ----- core objects ---------------------------------------------------
    Microsoft::WRL::ComPtr<ID3D12Device>            device_;
    Microsoft::WRL::ComPtr<IDXGIFactory6>           dxgi_factory_;
    Microsoft::WRL::ComPtr<IDXGISwapChain4>         swap_chain_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue>      cmd_queue_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator>  cmd_alloc_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmd_list_;

    // RTV descriptor heap + cached backbuffer resources + per-RT RTVs
    // (slots [kBackBufferCount .. kBackBufferCount + kRtvHeapExtra)).
    static constexpr UINT kRtvHeapExtra = 32;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>    rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource>          back_buffers_[kBackBufferCount];
    UINT rtv_descriptor_size_ = 0;
    UINT next_rtv_slot_ = kBackBufferCount;  // first free RTV slot for create_render_target

    // Two SRV/CBV/UAV heaps:
    //  - `srv_cpu_heap_`  (NOT shader-visible): where create_texture /
    //    create_render_target write CreateShaderResourceView at alloc
    //    time. CPU-readable, so we can use entries here as COPY SOURCE.
    //  - `srv_heap_`      (shader-visible scratch ring): per-draw
    //    descriptor tables get assembled here via CopyDescriptorsSimple
    //    from `srv_cpu_heap_`. Bound via SetDescriptorHeaps + the table
    //    GPU handle for SetGraphicsRootDescriptorTable.
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>    srv_cpu_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>    srv_heap_;
    UINT srv_descriptor_size_ = 0;
    UINT next_srv_cpu_slot_ = 0;  // alloc cursor in srv_cpu_heap_
    UINT scratch_srv_next_  = 0;  // alloc cursor in srv_heap_ (wraps)

    // Root signature shared by all pipeline passes (1 CBV b0 + 1 table
    // with 2 SRVs t0,t1; 2 static samplers s0,s1 linear-clamp).
    Microsoft::WRL::ComPtr<ID3D12RootSignature>     root_sig_;

    // Constant buffer ring (UPLOAD heap). Each set_uniform_block grabs
    // the next slot, memcpys data, binds it via SetGraphicsRootConstantBufferView.
    Microsoft::WRL::ComPtr<ID3D12Resource>          cb_ring_;
    uint8_t* cb_ring_mapped_ = nullptr;  // persistently mapped
    UINT     cb_ring_next_   = 0;        // index of next free slot

    // GPU/CPU sync.
    Microsoft::WRL::ComPtr<ID3D12Fence>             fence_;
    UINT64 fence_value_ = 0;
    HANDLE fence_event_ = nullptr;

    HWND hwnd_ = nullptr;
    int  width_  = 0;
    int  height_ = 0;
    UINT current_back_buffer_ = 0;
    bool ready_     = false;
    bool in_frame_  = false;
    bool default_fb_bound_ = false;
    PassHandle bound_pass_{0};
    RenderTargetHandle bound_rt_{0};   // {0} when default FB is bound

    // ----- resource pools -------------------------------------------
    struct TextureEntry {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
        UINT srv_cpu_slot = UINT_MAX;      // index into srv_cpu_heap_
        DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
        int width = 0;
        int height = 0;
        bool borrowed_from_rt = false;     // true if SRV aliases an RT's resource
        RenderTargetHandle source_rt{0};   // tracks the RT for state coupling
    };
    struct RenderTargetEntry {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_RENDER_TARGET;
        UINT rtv_slot     = UINT_MAX;      // index into rtv_heap_
        UINT srv_cpu_slot = UINT_MAX;      // index into srv_cpu_heap_
        DXGI_FORMAT format = kIntermediateFormat;
        int width = 0;
        int height = 0;
    };
    struct PassEntry {
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_intermediate;  // targets kIntermediateFormat
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_backbuffer;    // targets kBackBufferFormat
        size_t uniform_block_bytes = 0;
        int    pass_index = -1;
        // Last CBV gpu address bound. Captured at set_uniform_block;
        // re-bound via SetGraphicsRootConstantBufferView on next draw.
        D3D12_GPU_VIRTUAL_ADDRESS last_cbv = 0;
        // Texture slot bindings (max 2). Store the CPU descriptor in
        // srv_cpu_heap_ that we'll Copy into the scratch shader-visible
        // heap at draw time.
        D3D12_CPU_DESCRIPTOR_HANDLE slot_cpu[2]{};
        bool slot_set[2]{false, false};
    };
    std::unordered_map<uint32_t, TextureEntry>      textures_;
    std::unordered_map<uint32_t, RenderTargetEntry> rts_;
    std::unordered_map<uint32_t, PassEntry>         passes_;
    uint32_t next_id_ = 1;

    // Helpers
    UINT alloc_rtv_slot();
    UINT alloc_srv_cpu_slot();          // persistent CPU heap allocation
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu_handle(UINT slot) const;
    D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu_handle(UINT slot) const;   // in srv_cpu_heap_
    D3D12_CPU_DESCRIPTOR_HANDLE scratch_cpu_handle(UINT slot) const;  // in srv_heap_
    D3D12_GPU_DESCRIPTOR_HANDLE scratch_gpu_handle(UINT slot) const;
    void transition_texture(TextureEntry& e, D3D12_RESOURCE_STATES new_state);
    void transition_rt(RenderTargetEntry& e, D3D12_RESOURCE_STATES new_state);

    // Diagnostic name reported by name() — filled in init() to include the
    // adapter description and feature level so logs are useful.
    char name_buf_[256] = "Direct3D 12 (uninitialized)";
};

} // namespace tubelight

#endif // TUBELIGHT_HAVE_D3D12
