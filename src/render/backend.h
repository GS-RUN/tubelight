// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)
//
// IRenderBackend — abstraction over the underlying graphics API so the
// pipeline can target OpenGL today and Direct3D 12 (planned, ADR-0002
// Phase 3b) tomorrow without touching `Pipeline::render_to_screen`.
//
// Scope of Phase 3a (current):
//   - Only the small set of per-frame state-mutating GL calls that
//     `Pipeline` makes directly are routed through the backend
//     (viewport, clear, default-framebuffer bind, fullscreen quad draw).
//   - FBO / Texture2D / ShaderProgram remain GL-specific concrete types
//     for now. They get abstracted in Phase 3b when D3D12 forces it.
//   - The factory only knows about `BackendKind::OpenGL`. The CLI flag
//     `--renderer gl` is parsed but accepts only that value.
//
// Adding a new backend later means: (a) implement IRenderBackend,
// (b) extend create_backend(), (c) add a new enumerator + CLI token.

#pragma once

#include "render/handle.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace tubelight {

enum class BackendKind {
    OpenGL,
    D3D12,   // Phase 3b — skeleton only (boots + clears + presents).
             //           Pipeline still requires GL backend (Phase 3c port pending).
};

// Init parameters bundled into a struct so future fields (HDR caps query,
// sync interval, debug layer toggle) don't churn the signature.
struct BackendInitParams {
    // Opaque OS window handle. On Windows == HWND. GL backend ignores it
    // (its GL context is owned by GLFW which is already current). D3D12
    // backend uses it for the swap chain.
    void* native_window_handle = nullptr;
    int width  = 0;
    int height = 0;
    // Enable the API debug layer (D3D12 debug layer / GL_KHR_debug). Costs
    // a few % CPU; off in release.
    bool enable_debug = false;
    // Phase 4a (D3D12 only): create the swap chain for DirectComposition
    // (CreateSwapChainForComposition + a DComp visual tree) instead of
    // binding it directly to the HWND. Required for a flip-model swap chain
    // to coexist with WS_EX_LAYERED|WS_EX_TRANSPARENT (cross-process
    // click-through). GL ignores it; D3D12 with composition=false keeps the
    // CreateSwapChainForHwnd path (shader-only / wgc-test).
    bool composition = false;
    // Phase 4a (D3D12 only): "layered ULW" present mode. Renders to a
    // composition swap chain (no HWND association — CreateSwapChainForHwnd
    // forbids WS_EX_LAYERED windows) but does NOT build a DirectComposition
    // display tree. The overlay instead reads each finished frame back with
    // capture_backbuffer() and blits it onto a WS_EX_LAYERED|TRANSPARENT
    // window via UpdateLayeredWindow — the only Win32 path that gives
    // cross-process click-through AND shows D3D-rendered content (it mirrors
    // the proven GL recipe). Mutually exclusive with `composition`. GL
    // ignores it.
    bool layered = false;
};

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    // Called once after the windowing layer (GLFW, etc.) has created the
    // window. For GL the context must already be current; for D3D12 the
    // HWND must be non-null. Returns false if backend resources could not
    // be created — the caller should treat that as fatal (or pick another
    // backend per the fallback policy).
    virtual bool init(const BackendInitParams& params) = 0;

    // Releases backend-owned GPU resources. Idempotent.
    virtual void shutdown() = 0;

    // Called when the window framebuffer size changes (resize / DPI).
    // GL backend no-ops; D3D12 resizes the swap chain buffers.
    virtual void resize(int width, int height) = 0;

    // Diagnostic identifier shown by --version + the in-app menu.
    virtual const char* name() const = 0;

    // ----- Per-frame state -----------------------------------------
    // Begin a frame: acquire next backbuffer if applicable, reset command
    // allocator / list. GL no-ops. D3D12 transitions backbuffer to RT state.
    virtual void begin_frame() = 0;

    // Bind the swap-chain / window framebuffer as the current render
    // target. GL: framebuffer 0. D3D12: the current backbuffer RTV.
    virtual void bind_default_framebuffer() = 0;

    // Set the viewport rectangle for subsequent draws.
    virtual void set_viewport(int x, int y, int w, int h) = 0;

    // Clear the colour attachment of the currently bound target.
    virtual void clear_color(float r, float g, float b, float a) = 0;

    // Draw the standard fullscreen triangle (covers the viewport) using
    // whatever vertex source the backend prefers — the only state the
    // caller must have already established is the bound shader + uniforms.
    virtual void draw_fullscreen_quad() = 0;

    // End the frame and present the swap chain. GL: glfwSwapBuffers is
    // called by the caller (kept that way to centralise GLFW use); GL's
    // end_frame() is a no-op. D3D12: closes the command list, executes
    // on the queue, transitions backbuffer to PRESENT, calls Present().
    virtual void end_frame() = 0;

    // Block until the GPU has finished all submitted work. GL: glFinish().
    // D3D12: wait_for_gpu_idle(). Used by the --bench harness to bound a
    // measurement window; not on the normal per-frame path.
    virtual void finish() {}

    // GPU-measured duration (milliseconds) of the most recently completed
    // begin_frame..end_frame, via API timestamp queries. Present/vsync
    // independent. Returns < 0 if unsupported or not yet measured. Only
    // meaningful when frame timing was enabled (see set_frame_timing).
    virtual double last_frame_gpu_ms() const { return -1.0; }

    // Enable/disable per-frame GPU timestamp queries (off by default — the
    // queries + readback add a small cost only wanted during --bench).
    virtual void set_frame_timing(bool /*enabled*/) {}

    // True iff this backend can drive the full 8-pass Pipeline. GL: yes.
    // D3D12 in Phase 3b: NO (HLSL pass ports + abstract resources land in
    // Phase 3c). Callers should check this before passing the backend to
    // Pipeline::set_backend().
    virtual bool supports_pipeline() const = 0;

    // ----- Phase 3c handle-based resource API ------------------------
    // Lifecycle is backend-owned. Handles returned by create_*() are
    // valid until destroy_*() or shutdown(). Reuse of freed ids is at
    // the backend's discretion (not guaranteed).
    //
    // Backends that haven't implemented Phase 3c yet (D3D12 today)
    // return invalid handles ({0}) and log a one-shot warning. Callers
    // SHOULD check supports_pipeline() before invoking.

    virtual TextureHandle      create_texture(const TextureDesc& desc) = 0;
    virtual RenderTargetHandle create_render_target(int w, int h, PixelFormat fmt) = 0;
    virtual PassHandle         create_pass(const PassDesc& desc) = 0;

    virtual void destroy_texture(TextureHandle h) = 0;
    virtual void destroy_render_target(RenderTargetHandle h) = 0;
    virtual void destroy_pass(PassHandle h) = 0;

    // Uploads tightly-packed RGBA8 bytes (rows top-to-bottom, no
    // padding) to a texture created with format RGBA8_UNORM. Width
    // and height must match the descriptor. Returns false on size
    // mismatch or unsupported format.
    virtual bool upload_texture_rgba8(TextureHandle h,
                                       const void* data,
                                       int width, int height) = 0;

    // Snapshot the current contents of a render target into a texture.
    // Both resources must already exist and have matching dimensions
    // and pixel format. Used by Pipeline's pass-5 history snapshot.
    virtual void copy_rt_to_texture(RenderTargetHandle src,
                                     TextureHandle dst) = 0;

    // Bind a render target as the current write destination. Passing an
    // invalid handle ({0}) is equivalent to bind_default_framebuffer()
    // — i.e. the swap-chain backbuffer.
    virtual void bind_render_target(RenderTargetHandle h) = 0;

    // Bind a pass (shader program + PSO + root signature on D3D12).
    // Must be called BEFORE bind_texture / set_uniform_block for the
    // bindings to apply to this pass.
    virtual void bind_pass(PassHandle h) = 0;

    // Bind a texture to slot N. Slot 0 is always the primary input
    // (u_source). Slot 1 (passes 5 and 6) is u_prev_frame /
    // u_bezel_tex respectively. See pass_uniforms.h:pass_texture_slot_count.
    virtual void bind_texture(int slot, TextureHandle h) = 0;

    // Upload the per-pass uniform block. `bytes` must equal the
    // uniform_block_bytes declared in the corresponding create_pass()
    // call (asserted in debug). Data is opaque to the backend.
    virtual void set_uniform_block(PassHandle h,
                                    const void* data,
                                    size_t bytes) = 0;

    // Read the swap-chain backbuffer back to CPU memory as RGBA8. Used
    // by `--screenshot` for deterministic offscreen capture (no DWM
    // compositor interference). The output buffer is tightly packed
    // (no row padding), origin top-left. Returns false if the backend
    // can't read back (e.g. backbuffer is HDR10 and we requested RGBA8).
    virtual bool capture_backbuffer(std::vector<uint8_t>& out_rgba,
                                     int& out_width, int& out_height) = 0;

    // ----- Resource interop helpers ----------------------------------
    // Get a TextureHandle view of an RT's color attachment, so the next
    // pass in a cascade can bind it as input. Backend may return a
    // borrowed/aliased handle that's valid as long as the RT is.
    virtual TextureHandle rt_as_texture(RenderTargetHandle h) = 0;

    // ----- GL-specific escape hatch (TODO_FUTURE) --------------------
    // overlay_mode_win.cpp owns a Texture2D directly (DXGI capture
    // result). It hands its GLuint to Pipeline; Pipeline asks the
    // backend to wrap it into a non-owning handle so the rest of the
    // pipeline can treat it like any other texture. The returned
    // handle does NOT own the GL texture — destroy_texture() on it is
    // a no-op for the underlying id.
    // D3D12 returns {0}; D3D12 callers must always upload via
    // upload_texture_rgba8 into a real owned handle.
    virtual TextureHandle wrap_external_gl_texture(uint32_t /*gl_id*/,
                                                    int /*w*/, int /*h*/) {
        return {0};
    }
};

// Factory. Returns nullptr if `kind` is unsupported in this build.
std::unique_ptr<IRenderBackend> create_backend(BackendKind kind);

// Parse the CLI / config string form ("gl", later "dx12") into the enum.
// Returns false if unrecognised.
bool parse_backend_kind(const char* token, BackendKind& out);

// Inverse of parse_backend_kind — for diagnostics.
const char* backend_kind_token(BackendKind kind);

} // namespace tubelight
