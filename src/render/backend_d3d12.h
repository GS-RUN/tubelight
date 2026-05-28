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

    // Phase 3c lands the HLSL pass ports + abstract resource handles that
    // let Pipeline drive D3D12. F3c-2 ships only the stubs below; full
    // implementations land in F3c-4.
    bool supports_pipeline() const override { return false; }

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

private:
    static constexpr UINT kBackBufferCount = 2;
    static constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    void wait_for_gpu_idle();
    bool create_swap_chain_resources(int width, int height);
    void destroy_swap_chain_resources();

    // ----- core objects ---------------------------------------------------
    Microsoft::WRL::ComPtr<ID3D12Device>            device_;
    Microsoft::WRL::ComPtr<IDXGIFactory6>           dxgi_factory_;
    Microsoft::WRL::ComPtr<IDXGISwapChain4>         swap_chain_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue>      cmd_queue_;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator>  cmd_alloc_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmd_list_;

    // RTV descriptor heap + cached backbuffer resources.
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>    rtv_heap_;
    Microsoft::WRL::ComPtr<ID3D12Resource>          back_buffers_[kBackBufferCount];
    UINT rtv_descriptor_size_ = 0;

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

    // Diagnostic name reported by name() — filled in init() to include the
    // adapter description and feature level so logs are useful.
    char name_buf_[256] = "Direct3D 12 (uninitialized)";
};

} // namespace tubelight

#endif // TUBELIGHT_HAVE_D3D12
