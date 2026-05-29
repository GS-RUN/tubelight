// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)

#include "render/backend_gl.h"

#include "core/gl_common.h"
#include "render/pass_uniforms.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

namespace tubelight {

GLBackend::~GLBackend() {
    shutdown();
}

bool GLBackend::init(const BackendInitParams& /*params*/) {
    if (ready_) return true;
    if (!quad_.create()) {
        std::fprintf(stderr, "[tubelight] GLBackend: FullscreenQuad::create failed\n");
        return false;
    }
    ready_ = true;
    return true;
}

void GLBackend::shutdown() {
    if (!ready_) return;
    if (timer_query_) { glDeleteQueries(1, &timer_query_); timer_query_ = 0; }
    quad_.destroy();
    ready_ = false;
}

void GLBackend::begin_frame() {
    if (!timing_enabled_) return;
    if (!timer_query_) glGenQueries(1, &timer_query_);
    // GL_TIME_ELAPSED measures GPU time between glBeginQuery/glEndQuery —
    // wraps render_to_screen's 8 passes. Present/vsync independent.
    glBeginQuery(GL_TIME_ELAPSED, timer_query_);
    timer_active_ = true;
}

void GLBackend::end_frame() {
    if (timer_active_) {
        glEndQuery(GL_TIME_ELAPSED);
        timer_active_ = false;
    }
}

void GLBackend::finish() {
    glFinish();
    if (timing_enabled_ && timer_query_) {
        GLuint64 ns = 0;
        glGetQueryObjectui64v(timer_query_, GL_QUERY_RESULT, &ns);
        last_gpu_ms_ = static_cast<double>(ns) / 1.0e6;
    }
}

void GLBackend::bind_default_framebuffer() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GLBackend::set_viewport(int x, int y, int w, int h) {
    glViewport(x, y, w, h);
}

void GLBackend::clear_color(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void GLBackend::draw_fullscreen_quad() {
    quad_.draw();
}

// ----- Phase 3c handle API ----------------------------------------------

namespace {

GLenum gl_internal_format(PixelFormat f) {
    switch (f) {
        case PixelFormat::RGBA8_UNORM:   return GL_RGBA8;
        case PixelFormat::RGBA16_FLOAT:  return GL_RGBA16F;
    }
    return GL_RGBA8;
}

std::string shader_path(const char* filename) {
#ifdef TUBELIGHT_SHADER_DIR
    return std::string(TUBELIGHT_SHADER_DIR) + "/" + filename;
#else
    return std::string("shaders/") + filename;
#endif
}

// Pipeline.cpp owns this list — duplicated here so GLBackend can resolve
// PassDesc::pass_index to a shader source filename. Order matches
// pipeline.h kPassFilenames (Pass -1 .. Pass 6).
const char* kPassFilenames[8] = {
    "pass_minus1_signal.frag",
    "pass0_analysis.frag",
    "pass1_dither_reconstruct.frag",
    "pass2_beam_scanlines.frag",
    "pass3_mask.frag",
    "pass4_bloom.frag",
    "pass5_temporal.frag",
    "pass6_composition.frag",
};

} // namespace

TextureHandle GLBackend::create_texture(const TextureDesc& d) {
    if (!ready_ || d.width <= 0 || d.height <= 0) return {0};
    TextureEntry e;
    if (!e.tex.create_empty(d.width, d.height, gl_internal_format(d.format))) {
        std::fprintf(stderr, "[tubelight][gl] create_texture %dx%d failed\n",
                     d.width, d.height);
        return {0};
    }
    e.format = d.format;
    const uint32_t id = next_id_++;
    textures_.emplace(id, std::move(e));
    return TextureHandle{id};
}

RenderTargetHandle GLBackend::create_render_target(int w, int h, PixelFormat fmt) {
    if (!ready_ || w <= 0 || h <= 0) return {0};
    RenderTargetEntry e;
    if (!e.fbo.create(w, h, gl_internal_format(fmt))) {
        std::fprintf(stderr, "[tubelight][gl] create_render_target %dx%d failed\n", w, h);
        return {0};
    }
    e.format = fmt;
    const uint32_t id = next_id_++;
    rts_.emplace(id, std::move(e));
    return RenderTargetHandle{id};
}

PassHandle GLBackend::create_pass(const PassDesc& d) {
    if (!ready_ || d.pass_index < 0 || d.pass_index >= 8) return {0};
    PassEntry e;
    const std::string fs = shader_path(kPassFilenames[d.pass_index]);
    if (!e.shader.build_from_files(std::string{}, fs)) {
        std::fprintf(stderr, "[tubelight][gl] create_pass shader build failed (%s): %s\n",
                     fs.c_str(), e.shader.get_error().c_str());
        return {0};
    }
    if (d.uniform_block_bytes != pass_uniforms_size(d.pass_index)) {
        std::fprintf(stderr,
            "[tubelight][gl] create_pass: uniform_block_bytes mismatch for pass %d "
            "(caller=%zu, expected=%zu)\n",
            d.pass_index, d.uniform_block_bytes, pass_uniforms_size(d.pass_index));
        // Continue — caller's value wins; assert in set_uniform_block will fire.
    }
    e.uniform_block_bytes = d.uniform_block_bytes;
    e.pass_index = d.pass_index;

    // Allocate the uniform buffer object. Sized to match the std140
    // block; GL_DYNAMIC_DRAW because we update it every frame.
    glGenBuffers(1, &e.ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, e.ubo);
    glBufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(e.uniform_block_bytes),
                  nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    // Wire the shader's `PassUniforms` block to binding point 0 (matches
    // the `layout(std140, binding = 0)` in every .frag).
    GLuint prog = e.shader.program_id();
    GLuint block_index = glGetUniformBlockIndex(prog, "PassUniforms");
    if (block_index != GL_INVALID_INDEX) {
        glUniformBlockBinding(prog, block_index, 0);
    } else {
        std::fprintf(stderr,
            "[tubelight][gl] create_pass: shader pass %d has no `PassUniforms` block\n",
            d.pass_index);
    }

    const uint32_t id = next_id_++;
    passes_.emplace(id, std::move(e));
    return PassHandle{id};
}

void GLBackend::destroy_texture(TextureHandle h) {
    // For borrowed entries, the underlying GL id belongs to someone else
    // (an FBO color attachment or an external Texture2D). erase() drops
    // the entry but never calls glDeleteTextures — Texture2D's RAII
    // dtor / FBO::destroy own that responsibility.
    textures_.erase(h.id);
}
void GLBackend::destroy_render_target(RenderTargetHandle h) {
    rts_.erase(h.id);
}
void GLBackend::destroy_pass(PassHandle h) {
    auto it = passes_.find(h.id);
    if (it != passes_.end()) {
        if (it->second.ubo != 0) {
            GLuint ubo = it->second.ubo;
            glDeleteBuffers(1, &ubo);
        }
        passes_.erase(it);
    }
    if (bound_pass_.id == h.id) bound_pass_ = {0};
}

bool GLBackend::upload_texture_rgba8(TextureHandle h, const void* data,
                                      int width, int height) {
    auto it = textures_.find(h.id);
    if (it == textures_.end() || !data) return false;
    if (it->second.borrowed) {
        std::fprintf(stderr, "[tubelight][gl] upload_texture_rgba8: refused on borrowed handle\n");
        return false;
    }
    if (it->second.format != PixelFormat::RGBA8_UNORM) {
        std::fprintf(stderr, "[tubelight][gl] upload_texture_rgba8: format mismatch\n");
        return false;
    }
    if (it->second.tex.width() != width || it->second.tex.height() != height) {
        std::fprintf(stderr, "[tubelight][gl] upload_texture_rgba8: size mismatch %dx%d vs %dx%d\n",
                     width, height, it->second.tex.width(), it->second.tex.height());
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, it->second.tex.id());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                    GL_RGBA, GL_UNSIGNED_BYTE, data);
    return true;
}

void GLBackend::copy_rt_to_texture(RenderTargetHandle src, TextureHandle dst) {
    auto sit = rts_.find(src.id);
    auto dit = textures_.find(dst.id);
    if (sit == rts_.end() || dit == textures_.end()) return;
    // Bind the source FBO as read framebuffer, then glCopyTexSubImage2D
    // pulls from it into the destination texture.
    sit->second.fbo.bind();
    glBindTexture(GL_TEXTURE_2D, dit->second.tex.id());
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
                        dit->second.tex.width(), dit->second.tex.height());
}

void GLBackend::bind_render_target(RenderTargetHandle h) {
    if (!h.is_valid()) {
        bind_default_framebuffer();
        return;
    }
    auto it = rts_.find(h.id);
    if (it == rts_.end()) {
        bind_default_framebuffer();
        return;
    }
    it->second.fbo.bind();
}

void GLBackend::bind_pass(PassHandle h) {
    auto it = passes_.find(h.id);
    if (it == passes_.end()) return;
    it->second.shader.use();
    // Bind the pass's UBO to binding point 0. The shader's PassUniforms
    // block is already wired to slot 0 in create_pass via
    // glUniformBlockBinding.
    if (it->second.ubo != 0) {
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, it->second.ubo);
    }
    bound_pass_ = h;
}

void GLBackend::bind_texture(int slot, TextureHandle h) {
    auto it = textures_.find(h.id);
    if (it == textures_.end()) return;
    const GLuint gl_id = it->second.borrowed
        ? it->second.borrowed_id
        : it->second.tex.id();
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(slot));
    glBindTexture(GL_TEXTURE_2D, gl_id);
    glActiveTexture(GL_TEXTURE0);
    // Tell the bound shader that the sampler is at this unit. Sampler
    // names are conventional: slot 0 = u_source, slot 1 = secondary
    // (u_prev_frame or u_bezel_tex per pass).
    if (bound_pass_.is_valid()) {
        auto pit = passes_.find(bound_pass_.id);
        if (pit != passes_.end()) {
            const char* name = nullptr;
            switch (slot) {
                case 0: name = "u_source"; break;
                case 1:
                    // Pass 5 → u_prev_frame; pass 6 → u_bezel_tex.
                    name = (pit->second.pass_index == 6) ? "u_prev_frame"
                         : (pit->second.pass_index == 7) ? "u_bezel_tex"
                         : "u_secondary";
                    break;
                default: break;
            }
            if (name) pit->second.shader.set_int(name, slot);
        }
    }
}

TextureHandle GLBackend::rt_as_texture(RenderTargetHandle h) {
    auto it = rts_.find(h.id);
    if (it == rts_.end()) return {0};
    // Build a borrowed entry pointing to the FBO's color attachment.
    // Valid as long as the RT is — caller doesn't destroy this handle.
    TextureEntry e;
    e.format       = it->second.format;
    e.borrowed     = true;
    e.borrowed_id  = it->second.fbo.texture();
    e.borrowed_w   = it->second.fbo.width();
    e.borrowed_h   = it->second.fbo.height();
    const uint32_t id = next_id_++;
    textures_.emplace(id, std::move(e));
    return TextureHandle{id};
}

bool GLBackend::capture_backbuffer(std::vector<uint8_t>& out_rgba,
                                    int& out_width, int& out_height) {
    // Read viewport so we know the framebuffer dimensions.
    GLint vp[4]{};
    glGetIntegerv(GL_VIEWPORT, vp);
    const int w = vp[2];
    const int h = vp[3];
    if (w <= 0 || h <= 0) return false;
    out_width  = w;
    out_height = h;
    out_rgba.assign(static_cast<size_t>(w) * h * 4, 0);
    // Read the front buffer (presented frame). After SwapBuffers GL_FRONT
    // is what the user sees; this avoids reading a partially-drawn back.
    glReadBuffer(GL_FRONT);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out_rgba.data());
    return true;
}

TextureHandle GLBackend::wrap_external_gl_texture(uint32_t gl_id, int w, int h) {
    TextureEntry e;
    e.format      = PixelFormat::RGBA8_UNORM;  // conventional; not actually used
    e.borrowed    = true;
    e.borrowed_id = gl_id;
    e.borrowed_w  = w;
    e.borrowed_h  = h;
    const uint32_t id = next_id_++;
    textures_.emplace(id, std::move(e));
    return TextureHandle{id};
}

void GLBackend::set_uniform_block(PassHandle h, const void* data, size_t bytes) {
    auto it = passes_.find(h.id);
    if (it == passes_.end() || !data || it->second.ubo == 0) return;
    assert(bytes == it->second.uniform_block_bytes &&
           "set_uniform_block: bytes != create_pass uniform_block_bytes");
    // Upload the POD bytes into the UBO. The std140 layout in the
    // shader matches the POD struct byte-for-byte (asserted by
    // static_assert in pass_uniforms.h).
    glBindBuffer(GL_UNIFORM_BUFFER, it->second.ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0,
                     static_cast<GLsizeiptr>(bytes), data);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

} // namespace tubelight
