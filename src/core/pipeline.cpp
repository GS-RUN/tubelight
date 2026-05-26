// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN

#include "core/pipeline.h"

#include <algorithm>
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

int connection_to_int(Connection c) {
    switch (c) {
        case Connection::RF:        return 0;
        case Connection::Composite: return 1;
        case Connection::SVideo:    return 2;
        case Connection::ScartRgb:  return 3;
        case Connection::Component: return 4;
        case Connection::RgbVga:    return 5;
    }
    return 5;
}

int noise_type_to_int(NoiseType n) {
    switch (n) {
        case NoiseType::Pixel: return 0;
        case NoiseType::Line:  return 1;
        case NoiseType::Rf:    return 2;
        case NoiseType::None:  return 3;
    }
    return 3;
}

void apply_uniforms_for_pass(ShaderProgram& sh,
                             int pass_index,
                             const Pipeline::GlobalParams& p,
                             int src_width,
                             int src_height,
                             float time,
                             const std::optional<SignalProfile>& signal) {
    // Common uniforms used by most shaders.
    sh.set_vec2("u_resolution",
                glm::vec2(static_cast<float>(src_width), static_cast<float>(src_height)));
    sh.set_int("u_pass_index", pass_index);

    switch (pass_index) {
        case 0: // Pass −1 — signal modeling
            if (signal.has_value()) {
                const auto& s = signal.value();
                sh.set_float("u_luma_mhz",            static_cast<float>(s.luma_mhz));
                sh.set_float("u_chroma_i_mhz",        static_cast<float>(s.chroma_i_mhz));
                sh.set_float("u_chroma_q_mhz",        static_cast<float>(s.chroma_q_mhz));
                sh.set_float("u_dot_crawl_strength",  static_cast<float>(s.dot_crawl_strength));
                sh.set_float("u_rainbow_banding",     static_cast<float>(s.rainbow_banding));
                sh.set_float("u_ringing_amount",      static_cast<float>(s.ringing_amount));
                sh.set_float("u_ghosting_offset_px",  static_cast<float>(s.ghosting_offset_pixels));
                sh.set_int  ("u_noise_type",          noise_type_to_int(s.noise_type));
                sh.set_float("u_noise_strength",      static_cast<float>(s.noise_strength));
                sh.set_int  ("u_signal_connection",   connection_to_int(s.connection));
            } else {
                // Default = clean RGB/VGA (Pass −1 becomes identity).
                sh.set_float("u_luma_mhz", 25.0f);
                sh.set_float("u_chroma_i_mhz", 25.0f);
                sh.set_float("u_chroma_q_mhz", 25.0f);
                sh.set_float("u_dot_crawl_strength", 0.0f);
                sh.set_float("u_rainbow_banding", 0.0f);
                sh.set_float("u_ringing_amount", 0.0f);
                sh.set_float("u_ghosting_offset_px", 0.0f);
                sh.set_int  ("u_noise_type", 3);
                sh.set_float("u_noise_strength", 0.0f);
                sh.set_int  ("u_signal_connection", 5);
            }
            sh.set_float("u_time", time);
            break;
        case 1: // Pass 0 — analysis
            sh.set_float("u_dither_detect_threshold", 0.15f);
            break;
        case 2: // Pass 1 — dither reconstruct
            sh.set_float("u_reconstruction_strength", 1.0f);
            break;
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
            sh.set_float("u_time",              time);
            // Warm-up curve: linearly to 1.0 over 180s, then clamped.
            sh.set_float("u_warmup",            std::min(1.0f, time / 180.0f));
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
        apply_uniforms_for_pass(sh, i, params_, output_width_, output_height_, time_, signal_snapshot_);

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

void Pipeline::apply_crt_profile(const CRTProfile& p) {
    params_.scanline_strength = static_cast<float>(p.scanline_strength);
    params_.gamma_crt         = 2.5f;

    switch (p.mask_type) {
        case MaskType::None:           params_.mask_type = 0; break;
        case MaskType::Shadow:         params_.mask_type = 1; break;
        case MaskType::ApertureGrille: params_.mask_type = 2; break;
        case MaskType::Slot:           params_.mask_type = 3; break;
        case MaskType::Diamond:        params_.mask_type = 4; break;
        case MaskType::CgwgMix:        params_.mask_type = 5; break;
        case MaskType::DotTrio:        params_.mask_type = 6; break;
    }

    // Map dot pitch (mm) to display pixel pitch. A 14-inch 4:3 tube has a
    // horizontal viewable width of ~285 mm; with a 1280-pixel-wide output that
    // gives ~4.5 px/mm. We approximate; this is refined in F7 with viewable
    // area accounting for diagonal_inches and aspect.
    if (p.dot_pitch_mm.has_value()) {
        constexpr float kPixelsPerMm = 4.5f;
        params_.mask_pitch_px = static_cast<float>(p.dot_pitch_mm.value()) * kPixelsPerMm;
        params_.mask_pitch_px = std::max(params_.mask_pitch_px, 1.5f);
    }

    // Monochrome profiles dial mask down to none in practice (mask handled by
    // glass + phosphor color, not a triad mask).
    if (p.phosphor_type == PhosphorType::P1 ||
        p.phosphor_type == PhosphorType::P3 ||
        p.phosphor_type == PhosphorType::P4 ||
        p.phosphor_type == PhosphorType::P31) {
        params_.mask_type = 0;
        params_.mask_strength = 0.0f;
    } else {
        params_.mask_strength = 0.55f;
    }

    // Vignette ties to screen curvature.
    switch (p.screen_curvature) {
        case ScreenCurvature::Flat:       params_.barrel_strength = 0.02f; params_.vignette_strength = 0.20f; break;
        case ScreenCurvature::Mild:       params_.barrel_strength = 0.06f; params_.vignette_strength = 0.35f; break;
        case ScreenCurvature::Aggressive: params_.barrel_strength = 0.12f; params_.vignette_strength = 0.55f; break;
    }
}

void Pipeline::apply_signal_profile(const SignalProfile& s) {
    signal_snapshot_ = s;
}

} // namespace tubelight
