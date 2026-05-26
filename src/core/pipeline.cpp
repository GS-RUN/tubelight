// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN

#include "core/pipeline.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace tubelight {

namespace {

constexpr const char* kDisplayNames[Pipeline::kPassCount] = {
    "Pass -1 (signal)",
    "Pass  0 (analysis)",
    "Pass  1 (dither reconstruct)",
    "Pass  2 (beam + scanlines)",
    "Pass  3 (mask)",
    "Pass  4 (bloom + halation)",
    "Pass  5 (temporal)",
    "Pass  6 (composition)",
};

std::string shader_path(const std::string& filename) {
#ifdef TUBELIGHT_SHADER_DIR
    return std::string(TUBELIGHT_SHADER_DIR) + "/" + filename;
#else
    return std::string("shaders/") + filename;
#endif
}

void apply_uniforms_for_pass(ShaderProgram& sh,
                             int pass_index,
                             const Pipeline::GlobalParams& p,
                             int src_width,
                             int src_height) {
    // Common uniforms used by most shaders.
    sh.set_vec2("u_resolution",
                glm::vec2(static_cast<float>(src_width), static_cast<float>(src_height)));
    sh.set_int("u_pass_index", pass_index);

    switch (pass_index) {
        case 3: // Pass 2 — beam + scanlines
            sh.set_float("u_scanline_strength", p.scanline_strength);
            sh.set_float("u_beam_width",        p.beam_width);
            sh.set_float("u_gamma_crt",         p.gamma_crt);
            break;
        case 4: // Pass 3 — mask
            sh.set_int  ("u_mask_type",     p.mask_type);
            sh.set_float("u_mask_strength", p.mask_strength);
            sh.set_float("u_mask_pitch_px", p.mask_pitch_px);
            break;
        case 5: // Pass 4 — bloom
            sh.set_float("u_bloom_strength",    p.bloom_strength);
            sh.set_float("u_halation_strength", p.halation_strength);
            break;
        case 7: // Pass 6 — composition
            sh.set_float("u_barrel_strength",   p.barrel_strength);
            sh.set_float("u_vignette_strength", p.vignette_strength);
            sh.set_float("u_gamma_display",     p.gamma_display);
            break;
        default:
            break;
    }
}

} // namespace

bool Pipeline::create(int output_width, int output_height) {
    output_width_  = output_width;
    output_height_ = output_height;

    if (!quad_.create()) {
        std::fprintf(stderr, "[tubelight] FullscreenQuad::create failed\n");
        return false;
    }

    for (int i = 0; i < kPassCount; ++i) {
        enabled_[static_cast<size_t>(i)] = true;
        if (!fbos_[static_cast<size_t>(i)].create(output_width, output_height, GL_RGBA16F)) {
            std::fprintf(stderr, "[tubelight] FBO create failed for pass %d\n", i);
            return false;
        }
    }

    return reload_all_shaders();
}

bool Pipeline::reload_all_shaders() {
    for (int i = 0; i < kPassCount; ++i) {
        if (!load_shader(i, kPassFilenames[i])) {
            return false;
        }
    }
    return true;
}

bool Pipeline::load_shader(int pass_index, const std::string& filename) {
    const std::string fs_path = shader_path(filename);
    auto& sh = shaders_[static_cast<size_t>(pass_index)];
    if (!sh.build_from_files(std::string{}, fs_path)) {
        std::fprintf(stderr, "[tubelight] shader %s failed: %s\n",
                     filename.c_str(), sh.get_error().c_str());
        return false;
    }
    return true;
}

void Pipeline::resize(int width, int height) {
    if (width == output_width_ && height == output_height_) {
        return;
    }
    output_width_  = width;
    output_height_ = height;
    for (auto& fbo : fbos_) {
        fbo.resize(width, height);
    }
}

void Pipeline::set_pass_enabled(int pass_index, bool enabled) {
    if (pass_index >= 0 && pass_index < kPassCount) {
        enabled_[static_cast<size_t>(pass_index)] = enabled;
    }
}

bool Pipeline::is_pass_enabled(int pass_index) const {
    if (pass_index < 0 || pass_index >= kPassCount) {
        return false;
    }
    return enabled_[static_cast<size_t>(pass_index)];
}

bool Pipeline::render_to_screen(GLuint source_tex) {
    if (output_width_ <= 0 || output_height_ <= 0) {
        return false;
    }

    GLuint current_input = source_tex;

    for (int i = 0; i < kPassCount; ++i) {
        if (!enabled_[static_cast<size_t>(i)]) {
            // Identity pass: the next pass reads from current_input directly.
            // We still bind the FBO to keep state consistent in case a later
            // disabled→enabled toggle expects a known texture.
            continue;
        }

        auto& sh = shaders_[static_cast<size_t>(i)];
        auto& fbo = fbos_[static_cast<size_t>(i)];

        const bool is_last = (i == kPassCount - 1);
        if (is_last) {
            // Last pass writes to default framebuffer.
            FBO::unbind();
            glViewport(0, 0, output_width_, output_height_);
        } else {
            fbo.bind();
        }

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        sh.use();
        apply_uniforms_for_pass(sh, i, params_, output_width_, output_height_);

        // Bind input texture to unit 0 ("u_source")
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, current_input);
        sh.set_int("u_source", 0);

        quad_.draw();

        if (!is_last) {
            current_input = fbo.texture();
        }
    }

    return true;
}

const char* pass_display_name(int pass_index) {
    if (pass_index < 0 || pass_index >= Pipeline::kPassCount) {
        return "unknown";
    }
    return kDisplayNames[pass_index];
}

} // namespace tubelight
