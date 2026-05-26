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
            sh.set_float("u_scanline_count",    p.scanline_count);
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
        case 6: // Pass 5 — temporal phosphor persistence
            sh.set_vec3 ("u_persistence",
                          glm::vec3(p.persistence_strength * p.persistence_ratio_r,
                                    p.persistence_strength * p.persistence_ratio_g,
                                    p.persistence_strength * p.persistence_ratio_b));
            // u_history_valid is set by the caller in render_to_screen()
            // because it depends on pipeline state, not just params.
            break;
        case 7: // Pass 6 — composition
            sh.set_float("u_barrel_strength",   p.barrel_strength);
            sh.set_float("u_vignette_strength", p.vignette_strength);
            sh.set_float("u_gamma_display",     p.gamma_display);
            sh.set_float("u_time",              time);
            sh.set_float("u_warmup",            std::min(1.0f, time / 180.0f));
            sh.set_int  ("u_monochrome",        p.monochrome);
            sh.set_int  ("u_posterize_levels",  p.posterize_levels);
            sh.set_vec3 ("u_phosphor_color",
                          glm::vec3(p.phosphor_color_r, p.phosphor_color_g, p.phosphor_color_b));
            sh.set_vec3 ("u_glass_tint",
                          glm::vec3(p.glass_tint_r, p.glass_tint_g, p.glass_tint_b));
            sh.set_float("u_glass_age",         p.glass_age);
            sh.set_float("u_target_aspect",     p.target_aspect);
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

    // History FBO for Pass 5's temporal persistence. Same size + format as
    // the pass FBOs so glCopyTexSubImage2D from pass 5's bound FBO works.
    if (!history_fbo_.create(output_width, output_height, GL_RGBA16F)) {
        std::fprintf(stderr, "[tubelight] history FBO create failed\n");
        return false;
    }
    history_valid_ = false;

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
    history_fbo_.resize(width, height);
    // Resized texture contains garbage / old size — don't blend with it.
    history_valid_ = false;
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
            // For pass 5 specifically, also invalidate history so an enable
            // after a disable doesn't blend in completely stale content.
            if (i == 6) history_valid_ = false;
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

        // Pass 5 also samples the previous frame's output as `u_prev_frame`
        // on texture unit 1, so the shader can blend with exponential decay
        // to fake the phosphor's afterglow.
        if (i == 6) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, history_fbo_.texture());
            sh.set_int("u_prev_frame", 1);
            sh.set_int("u_history_valid", history_valid_ ? 1 : 0);
            glActiveTexture(GL_TEXTURE0);
        }

        quad_.draw();

        // After pass 5 has rendered into fbos_[6], snapshot it into
        // history_fbo_ so the NEXT frame's pass 5 can sample it as
        // u_prev_frame. glCopyTexSubImage2D reads from whatever framebuffer
        // is currently bound — fbos_[6] is still bound at this point.
        if (i == 6) {
            glBindTexture(GL_TEXTURE_2D, history_fbo_.texture());
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
                                 output_width_, output_height_);
            history_valid_ = true;
        }

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
    // Use the profile's native scanline_strength at face value. The menu's
    // intensity slider scales it further if the user wants something subtler.
    params_.scanline_strength = static_cast<float>(p.scanline_strength);
    params_.gamma_crt         = 2.5f;

    // Aspect ratio: parse "4:3", "5:4", "16:9" etc. → target width/height.
    params_.target_aspect = 0.0f;
    if (p.aspect_native == "4:3")  params_.target_aspect = 4.0f / 3.0f;
    else if (p.aspect_native == "5:4")  params_.target_aspect = 5.0f / 4.0f;
    else if (p.aspect_native == "16:9") params_.target_aspect = 16.0f / 9.0f;

    switch (p.mask_type) {
        case MaskType::None:           params_.mask_type = 0; break;
        case MaskType::Shadow:         params_.mask_type = 1; break;
        case MaskType::ApertureGrille: params_.mask_type = 2; break;
        case MaskType::Slot:           params_.mask_type = 3; break;
        case MaskType::Diamond:        params_.mask_type = 4; break;
        case MaskType::CgwgMix:        params_.mask_type = 5; break;
        case MaskType::DotTrio:        params_.mask_type = 6; break;
    }

    // Map dot pitch (mm) to display pixel pitch.
    if (p.dot_pitch_mm.has_value()) {
        constexpr float kPixelsPerMm = 4.5f;
        params_.mask_pitch_px = static_cast<float>(p.dot_pitch_mm.value()) * kPixelsPerMm;
        params_.mask_pitch_px = std::max(params_.mask_pitch_px, 1.5f);
    }

    // Monochrome handling: no triad mask, instead colour the output using
    // the phosphor's chromaticity (Pass 6 reads u_phosphor_color when
    // u_monochrome is on).
    const bool is_mono =
        (p.phosphor_type == PhosphorType::P1 ||
         p.phosphor_type == PhosphorType::P3 ||
         p.phosphor_type == PhosphorType::P4 ||
         p.phosphor_type == PhosphorType::P31);

    params_.monochrome = is_mono ? 1 : 0;
    // Mac-Classic-class profiles want literal 1-bit; detect by id prefix.
    const bool is_mac_classic =
        p.id.find("mac-classic") != std::string::npos ||
        p.id.find("apple-lisa")  != std::string::npos;

    if (is_mono) {
        params_.mask_type = 0;
        params_.mask_strength = 0.0f;
        // Default posterize: text terminals quantised to 6 steps (less than
        // analog TV, more than pure 1-bit). Profiles can override this
        // through their JSON if they want a different look.
        params_.posterize_levels = 6;

        // Locked monochrome preset. Two flavours depending on what kind
        // of monochrome tube we're emulating:
        //
        //   - "Terminal" monochromes (P31 green, P3 amber, P1 scope,
        //     Mac Classic / Lisa B&W): RGB-direct connection, sharp
        //     letters, light scanlines (~350 raster), low glow.
        //   - "TV" monochrome (tv-bw-p4, analog B&W broadcast TV):
        //     visible 240-line NTSC raster, heavier beam bloom, slight
        //     curvature, posterize off (analog continuous gradient).
        //
        // The phosphor type alone isn't enough to distinguish (Mac
        // Classic is also P4) — we use the is_mac_classic flag computed
        // above so Macs stay in the terminal preset.
        const bool is_tv_bw = (p.phosphor_type == PhosphorType::P4 && !is_mac_classic);

        if (is_tv_bw) {
            params_.scanline_strength = 0.45f; // visible TV scanlines
            params_.scanline_count    = 240.0f; // NTSC raster
            params_.beam_width        = 1.60f;
            params_.gamma_crt         = 2.4f;
            params_.gamma_display     = 2.2f;
            params_.bloom_strength    = 0.18f; // bigger phosphor glow
            params_.halation_strength = 0.0f;
            params_.barrel_strength   = 0.035f; // mild TV curvature
            params_.vignette_strength = 0.22f;
            // B&W TV phosphor (P4) has longer perceived persistence than
            // a terminal — that "glow trail" when something moves on screen.
            params_.persistence_strength = 0.40f;
            params_.persistence_ratio_r  = 1.0f;
            params_.persistence_ratio_g  = 1.0f;
            params_.persistence_ratio_b  = 1.0f;
        } else {
            params_.scanline_strength = 0.18f; // soft terminal lines
            params_.scanline_count    = 350.0f; // text-terminal raster
            params_.beam_width        = 1.40f;
            params_.gamma_crt         = 2.2f;  // sRGB round-trip
            params_.gamma_display     = 2.2f;
            params_.bloom_strength    = 0.10f;
            params_.halation_strength = 0.0f;
            params_.barrel_strength   = 0.018f;
            params_.vignette_strength = 0.10f;
            // P31 / P3 terminals have a moderate phosphor decay — visible
            // afterglow but not as long as a B&W TV. Mac Classic / Lisa
            // intentionally inherit this so 1-bit graphics still streak
            // a touch under fast scrolling.
            params_.persistence_strength = 0.28f;
            params_.persistence_ratio_r  = 1.0f;
            params_.persistence_ratio_g  = 1.0f;
            params_.persistence_ratio_b  = 1.0f;
        }

        // Force every pass on so any earlier user toggling doesn't leave
        // the locked preset partially disabled.
        for (int i = 0; i < kPassCount; ++i) set_pass_enabled(i, true);
        switch (p.phosphor_type) {
            case PhosphorType::P31: // bright green (Apple II / VT100 class)
                params_.phosphor_color_r = 0.10f;
                params_.phosphor_color_g = 1.30f;
                params_.phosphor_color_b = 0.20f;
                break;
            case PhosphorType::P3:  // amber (IBM 5151 / HP)
                params_.phosphor_color_r = 1.40f;
                params_.phosphor_color_g = 0.65f;
                params_.phosphor_color_b = 0.05f;
                break;
            case PhosphorType::P1:  // deep green oscilloscope tube
                params_.phosphor_color_r = 0.05f;
                params_.phosphor_color_g = 1.30f;
                params_.phosphor_color_b = 0.15f;
                break;
            case PhosphorType::P4:  // B&W: analog TV by default → no posterize.
                params_.phosphor_color_r = 0.95f;
                params_.phosphor_color_g = 1.00f;
                params_.phosphor_color_b = 1.10f;
                params_.posterize_levels = is_mac_classic ? 2 : 0;
                break;
            default: break;
        }
    } else {
        params_.mask_strength = 0.40f; // visible mask for colour CRTs
        params_.phosphor_color_r = 1.0f;
        params_.phosphor_color_g = 1.0f;
        params_.phosphor_color_b = 1.0f;
        // Colour CRTs (P22): red phosphor lingers much longer than green
        // and blue — that's what produces the "warm trail" on bright
        // moving objects (the signature look of arcade CRTs). Ratios
        // mirror the per-channel decay_ms in the profile (R ~1.0 ms,
        // G/B ~0.08 ms → ratio 1.0 : 0.5 : 0.5 perceptually).
        params_.persistence_strength = 0.30f;
        params_.persistence_ratio_r  = 1.0f;
        params_.persistence_ratio_g  = 0.5f;
        params_.persistence_ratio_b  = 0.5f;
    }

    // Glass tint / age — picks up the aged-yellow look of vintage B&W TVs etc.
    if (p.glass_tint.size() == 3) {
        params_.glass_tint_r = static_cast<float>(p.glass_tint[0]);
        params_.glass_tint_g = static_cast<float>(p.glass_tint[1]);
        params_.glass_tint_b = static_cast<float>(p.glass_tint[2]);
    } else {
        params_.glass_tint_r = params_.glass_tint_g = params_.glass_tint_b = 1.0f;
    }
    params_.glass_age = static_cast<float>(p.glass_age);

    // Curvature → barrel + vignette. Conservative so corners aren't cut on
    // a desktop overlay, but visibly different across profiles. Skipped
    // for monochrome profiles — the locked preset above already set
    // barrel + vignette to look right for terminals.
    if (!is_mono) {
        switch (p.screen_curvature) {
            case ScreenCurvature::Flat:       params_.barrel_strength = 0.010f; params_.vignette_strength = 0.08f; break;
            case ScreenCurvature::Mild:       params_.barrel_strength = 0.035f; params_.vignette_strength = 0.20f; break;
            case ScreenCurvature::Aggressive: params_.barrel_strength = 0.080f; params_.vignette_strength = 0.40f; break;
        }
    } else {
        // Clear any previously-loaded signal profile: monochrome CRTs were
        // RGB-direct (TTL or VGA), never composite. Pass −1 falls back to
        // its identity defaults so no NTSC/PAL artifacts contaminate the
        // clean phosphor look.
        signal_snapshot_ = std::nullopt;
        params_.scanline_count = 350.0f;
    }
}

void Pipeline::apply_signal_profile(const SignalProfile& s) {
    if (params_.monochrome == 1) {
        // Monochrome CRTs are locked to a clean RGB-direct signal path.
        // Ignore the requested signal profile so the user can't
        // accidentally pair P31 / Mac Classic / amber terminal with
        // composite NTSC and get smearing + dot crawl.
        return;
    }
    signal_snapshot_ = s;
    // Pick a sensible default visible scanline count from the signal standard.
    // The user can override via the menu slider.
    switch (s.standard) {
        case Standard::NtscM:  params_.scanline_count = 240.0f; break;
        case Standard::PalBg:  params_.scanline_count = 288.0f; break;
        case Standard::PalN:   params_.scanline_count = 288.0f; break;
        case Standard::Secam:  params_.scanline_count = 288.0f; break;
        case Standard::None:   params_.scanline_count = 480.0f; break; // VGA / RGB pure
    }
}

} // namespace tubelight
