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
// Synchronisation: N-frame-in-flight pacing (post-3d). One command
// allocator per back buffer + a per-slot fence value; begin_frame only
// stalls when the CPU laps the GPU by kBackBufferCount frames, instead of
// the Phase 3b skeleton's wait-for-idle on every Present. Load-time paths
// (upload / create_pass / capture / resize / shutdown) still do a full
// wait_for_gpu_idle() — they are not on the per-frame hot path.

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

// DirectComposition (Phase 4a) — composition swap chain for the
// click-through overlay. Only used when BackendInitParams::composition.
#include <dcomp.h>

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
    void finish() override;
    double last_frame_gpu_ms() const override { return last_gpu_ms_; }
    void set_frame_timing(bool enabled) override;

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

    // ----- Phase 4a.3 accessors for the ImGui D3D12 backend -----------
    // The in-app menu's ImGui_ImplDX12 needs the device + queue (texture
    // uploads) at init and the current frame's command list at draw time
    // (between render_to_screen and end_frame, with the backbuffer RTV
    // still bound). Non-owning.
    ID3D12Device*              device()        const { return device_.Get(); }
    ID3D12CommandQueue*        command_queue() const { return cmd_queue_.Get(); }
    ID3D12GraphicsCommandList* command_list()  const { return cmd_list_.Get(); }
    static constexpr DXGI_FORMAT backbuffer_format() { return kBackBufferFormat; }
    static constexpr UINT        frames_in_flight()  { return kBackBufferCount; }

    // Runtime vsync toggle (menu "low latency"): on → Present(1,0), off →
    // Present(0,0). Default on.
    void set_vsync(bool on) { vsync_ = on; }
    bool vsync() const { return vsync_; }

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
    // Block until the shared fence has reached `value` (no new signal).
    // Used by begin_frame to wait only when the CPU laps the GPU by
    // kBackBufferCount frames (DX-10 + DX-22 frame pacing).
    void wait_for_fence(UINT64 value);
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

    // DirectComposition visual tree (Phase 4a). Non-null only when the
    // backend was init'd with composition=true; holds the swap chain as the
    // content of a single full-window visual.
    bool composition_ = false;
    // Layered ULW present mode (BackendInitParams::layered): composition swap
    // chain for rendering, but NO DComp display tree — the overlay presents
    // the captured frame via UpdateLayeredWindow. See backend.h.
    bool layered_ = false;
    bool vsync_ = true;   // Present sync interval: true→1, false→0
    Microsoft::WRL::ComPtr<IDCompositionDevice>     dcomp_device_;
    Microsoft::WRL::ComPtr<IDCompositionTarget>     dcomp_target_;
    Microsoft::WRL::ComPtr<IDCompositionVisual>     dcomp_visual_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue>      cmd_queue_;
    // One command allocator per frame in flight (DX-22). begin_frame
    // resets cmd_alloc_[current_back_buffer_] only after the fence proves
    // that frame's prior GPU work is done (DX-10). The single command
    // list is reset onto the slot's allocator each frame.
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator>  cmd_alloc_[kBackBufferCount];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmd_list_;

    // RTV descriptor heap + cached backbuffer resources + per-RT RTVs
    // (slots [kBackBufferCount .. kBackBufferCount + kRtvHeapExtra)).
    static constexpr UINT kRtvHeapExtra = 32;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>    rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource>          back_buffers_[kBackBufferCount];
    UINT rtv_descriptor_size_ = 0;
    UINT next_rtv_slot_ = kBackBufferCount;  // bump alloc; free list reused first
    // Free lists so destroy_render_target / destroy_texture return their RTV +
    // CPU-SRV slots for reuse. Without these, every Pipeline::resize (which
    // recreates all pass RTs) leaked ~N RTV slots → "RTV heap exhausted" after
    // a handful of resizes (e.g. a Ctrl-drag resize).
    std::vector<UINT> free_rtv_slots_;
    std::vector<UINT> free_srv_cpu_slots_;

    // Two SRV/CBV/UAV heaps:
    //  - `srv_cpu_heap_`  (NOT shader-visible): where create_texture /
    //    create_render_target write CreateShaderResourceView at alloc
    //    time. CPU-readable, so we can use entries here as COPY SOURCE.
    //  - `srv_heap_`      (shader-visible): holds PERSISTENT per-pass
    //    descriptor tables (2 SRVs each, double-buffered per frame in
    //    flight). draw_fullscreen_quad re-copies from `srv_cpu_heap_`
    //    ONLY when a pass's bound textures change (steady state: 0 copies),
    //    then binds the pass's fixed table GPU handle. Replaces the old
    //    per-draw CopyDescriptorsSimple into a wrapping scratch ring.
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>    srv_cpu_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>    srv_heap_;
    UINT srv_descriptor_size_ = 0;
    UINT next_srv_cpu_slot_ = 0;  // alloc cursor in srv_cpu_heap_
    UINT next_srv_gpu_slot_ = 0;  // persistent alloc cursor in srv_heap_

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
    // Fence value signaled at the end of the frame that last used each
    // in-flight slot. begin_frame waits for frame_fence_value_[slot]
    // before recycling that slot's allocator + ring slots.
    UINT64 frame_fence_value_[kBackBufferCount] = {};
    HANDLE fence_event_ = nullptr;

    // --bench: 2 GPU timestamps (frame start/end) resolved to a READBACK
    // buffer. Present/vsync independent. Off unless set_frame_timing(true).
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> ts_query_heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource>  ts_readback_;
    UINT64 ts_frequency_   = 0;       // queue ticks per second
    bool   timing_enabled_ = false;
    bool   ts_wrote_       = false;   // a query pair was written this frame
    double last_gpu_ms_    = -1.0;

    // capture_backbuffer: PERSISTENT readback path for the layered present.
    // Recreated only when the backbuffer size changes — NOT per frame (the old
    // per-frame CreateCommittedResource + CreateCommandList capped the windowed
    // overlay at ~30fps). The copy is submitted on its own allocator/list, then
    // we wait on cap_fence_value_ (just the copy, not a full pipeline flush).
    Microsoft::WRL::ComPtr<ID3D12Resource>            cap_readback_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator>    cap_alloc_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cap_list_;
    UINT64                            cap_total_bytes_ = 0;
    UINT                              cap_num_rows_    = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT cap_layout_{};
    int                               cap_w_ = 0;     // size the readback was sized for
    int                               cap_h_ = 0;
    UINT64                            cap_fence_value_ = 0;

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
        // Texture slot bindings (max 2) recorded by bind_texture each
        // frame: the source CPU descriptor in srv_cpu_heap_ for each slot.
        D3D12_CPU_DESCRIPTOR_HANDLE slot_cpu[2]{};
        bool slot_set[2]{false, false};
        // PERSISTENT per-pass descriptor tables in the shader-visible heap,
        // double-buffered per frame in flight to avoid overwriting a table
        // the GPU is still reading. gpu_table_slot[f] is the base slot of a
        // 2-SRV table; baked_cpu[f][s] caches what was last copied there so
        // draw_fullscreen_quad re-copies only on a binding change.
        UINT gpu_table_slot[kBackBufferCount];
        D3D12_CPU_DESCRIPTOR_HANDLE baked_cpu[kBackBufferCount][2]{};
        bool baked[kBackBufferCount][2]{};
        PassEntry() {
            for (UINT i = 0; i < kBackBufferCount; ++i) gpu_table_slot[i] = UINT_MAX;
        }
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
