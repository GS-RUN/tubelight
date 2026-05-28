// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)
//
// OpenGL implementation of IRenderBackend. Wraps the existing
// `FullscreenQuad` VAO so Phase 3a is a pure re-routing change with
// zero functional difference vs. v0.1.6.

#pragma once

#include "core/fbo.h"
#include "core/quad.h"
#include "core/shader.h"
#include "core/texture.h"
#include "render/backend.h"

#include <cstdint>
#include <unordered_map>

namespace tubelight {

class GLBackend final : public IRenderBackend {
public:
    GLBackend() = default;
    ~GLBackend() override;

    GLBackend(const GLBackend&) = delete;
    GLBackend& operator=(const GLBackend&) = delete;

    bool init(const BackendInitParams& params) override;
    void shutdown() override;
    void resize(int width, int height) override { (void)width; (void)height; }
    const char* name() const override { return "OpenGL 4.5 core (libepoxy)"; }

    void begin_frame() override {}
    void bind_default_framebuffer() override;
    void set_viewport(int x, int y, int w, int h) override;
    void clear_color(float r, float g, float b, float a) override;
    void draw_fullscreen_quad() override;
    void end_frame() override {}

    // GLBackend fully drives Pipeline — that's its job today.
    bool supports_pipeline() const override { return true; }

    // ----- Phase 3c handle API ---------------------------------------
    TextureHandle      create_texture(const TextureDesc&) override;
    RenderTargetHandle create_render_target(int w, int h, PixelFormat) override;
    PassHandle         create_pass(const PassDesc&) override;

    void destroy_texture(TextureHandle) override;
    void destroy_render_target(RenderTargetHandle) override;
    void destroy_pass(PassHandle) override;

    bool upload_texture_rgba8(TextureHandle, const void* data,
                               int width, int height) override;
    void copy_rt_to_texture(RenderTargetHandle src, TextureHandle dst) override;

    void bind_render_target(RenderTargetHandle) override;
    void bind_pass(PassHandle) override;
    void bind_texture(int slot, TextureHandle) override;
    void set_uniform_block(PassHandle, const void* data, size_t bytes) override;
    uint32_t gl_color_attachment(RenderTargetHandle) const override;

private:
    // Each create_* call returns a strictly-increasing id; lookups are
    // a single hash hit. Lifetime owned by these maps.
    struct TextureEntry {
        Texture2D tex;
        PixelFormat format = PixelFormat::RGBA8_UNORM;
    };
    struct RenderTargetEntry {
        FBO fbo;
        PixelFormat format = PixelFormat::RGBA16_FLOAT;
    };
    struct PassEntry {
        ShaderProgram shader;
        size_t uniform_block_bytes = 0;
        int    pass_index = -1;
        // Phase 3c: scalar/vec uniforms live in a `layout(std140, binding=0)
        // uniform PassUniforms { ... }` block. GL UBOs back this — one
        // buffer per pass, sized to match the std140 layout pinned in the
        // shader and the matching POD in pass_uniforms.h.
        unsigned int ubo = 0;
    };

    FullscreenQuad quad_;
    bool ready_ = false;

    std::unordered_map<uint32_t, TextureEntry>       textures_;
    std::unordered_map<uint32_t, RenderTargetEntry>  rts_;
    std::unordered_map<uint32_t, PassEntry>          passes_;
    uint32_t next_id_ = 1;  // id == 0 reserved as invalid

    // The pass currently bound by bind_pass(). set_uniform_block reads
    // this to dispatch the uniforms onto the right shader.
    PassHandle bound_pass_{0};
};

} // namespace tubelight
