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

#include <memory>

namespace tubelight {

enum class BackendKind {
    OpenGL,
    // D3D12,   // Phase 3b
};

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    // Called once after the windowing layer (GLFW, etc.) has made the
    // graphics context current. Returns false if backend resources
    // could not be created — the caller should treat that as fatal.
    virtual bool init() = 0;

    // Releases backend-owned GPU resources. Idempotent.
    virtual void shutdown() = 0;

    // Diagnostic identifier shown by --version + the in-app menu.
    virtual const char* name() const = 0;

    // ----- Per-frame state -----------------------------------------
    // Bind the swap-chain / window framebuffer as the current render
    // target. GL: framebuffer 0. D3D12 (future): the current backbuffer.
    virtual void bind_default_framebuffer() = 0;

    // Set the viewport rectangle for subsequent draws.
    virtual void set_viewport(int x, int y, int w, int h) = 0;

    // Clear the colour attachment of the currently bound target.
    virtual void clear_color(float r, float g, float b, float a) = 0;

    // Draw the standard fullscreen triangle (covers the viewport) using
    // whatever vertex source the backend prefers — the only state the
    // caller must have already established is the bound shader + uniforms.
    virtual void draw_fullscreen_quad() = 0;
};

// Factory. Returns nullptr if `kind` is unsupported in this build.
std::unique_ptr<IRenderBackend> create_backend(BackendKind kind);

// Parse the CLI / config string form ("gl", later "dx12") into the enum.
// Returns false if unrecognised.
bool parse_backend_kind(const char* token, BackendKind& out);

// Inverse of parse_backend_kind — for diagnostics.
const char* backend_kind_token(BackendKind kind);

} // namespace tubelight
