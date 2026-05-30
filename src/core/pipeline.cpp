// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)

#include "core/pipeline.h"

#include "core/gl_common.h"
#include "core/texture.h"
#include "render/pass_uniforms.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
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

// Phase 3c: per-pass uniforms are now POD structs (see render/pass_uniforms.h)
// matching the std140 cbuffer in each .frag. Pipeline fills the struct,
// hands it to backend->set_uniform_block(); the backend chooses how to
// upload (glUniform* on GL, MapAndCopy on D3D12).
//
// `history_valid` for pass 5 is passed in because it depends on pipeline
// state, not the GlobalParams snapshot.
void build_uniforms_for_pass(int pass_index,
                              const Pipeline::GlobalParams& p,
                              int src_width,
                              int src_height,
                              float time,
                              float frame_mean_lum,
                              bool history_valid,
                              const std::optional<SignalProfile>& signal,
                              void* out_buffer) {
    const float res_x = static_cast<float>(src_width);
    const float res_y = static_cast<float>(src_height);

    switch (pass_index) {
        case 0: { // Pass -1 signal
            auto& u = *static_cast<PassUniforms_PassMinus1*>(out_buffer);
            u = {};
            u.u_resolution[0] = res_x; u.u_resolution[1] = res_y;
            if (signal.has_value()) {
                const auto& s = signal.value();
                u.u_luma_mhz            = static_cast<float>(s.luma_mhz);
                u.u_chroma_i_mhz        = static_cast<float>(s.chroma_i_mhz);
                u.u_chroma_q_mhz        = static_cast<float>(s.chroma_q_mhz);
                u.u_dot_crawl_strength  = static_cast<float>(s.dot_crawl_strength);
                u.u_rainbow_banding     = static_cast<float>(s.rainbow_banding);
                u.u_ringing_amount      = static_cast<float>(s.ringing_amount);
                u.u_ghosting_offset_px  = static_cast<float>(s.ghosting_offset_pixels);
                u.u_noise_type          = noise_type_to_int(s.noise_type);
                u.u_noise_strength      = static_cast<float>(s.noise_strength);
                u.u_signal_connection   = connection_to_int(s.connection);
            } else {
                // Default = clean RGB/VGA (Pass -1 becomes identity).
                u.u_luma_mhz = u.u_chroma_i_mhz = u.u_chroma_q_mhz = 25.0f;
                u.u_noise_type        = 3;
                u.u_signal_connection = 5;
            }
            u.u_time = time;
            break;
        }
        case 1: { // Pass 0 analysis
            auto& u = *static_cast<PassUniforms_Pass0*>(out_buffer);
            u = {};
            u.u_resolution[0] = res_x; u.u_resolution[1] = res_y;
            u.u_dither_detect_threshold = 0.15f;
            break;
        }
        case 2: { // Pass 1 dither reconstruct
            auto& u = *static_cast<PassUniforms_Pass1*>(out_buffer);
            u = {};
            u.u_resolution[0] = res_x; u.u_resolution[1] = res_y;
            u.u_reconstruction_strength = 1.0f;
            break;
        }
        case 3: { // Pass 2 beam + scanlines
            auto& u = *static_cast<PassUniforms_Pass2*>(out_buffer);
            u = {};
            u.u_resolution[0] = res_x; u.u_resolution[1] = res_y;
            u.u_scanline_strength = p.scanline_strength;
            u.u_beam_width        = p.beam_width;
            u.u_gamma_crt         = p.gamma_crt;
            u.u_scanline_count    = p.scanline_count;
            u.u_frame_mean_lum    = frame_mean_lum;
            break;
        }
        case 4: { // Pass 3 mask
            auto& u = *static_cast<PassUniforms_Pass3*>(out_buffer);
            u = {};
            u.u_resolution[0] = res_x; u.u_resolution[1] = res_y;
            u.u_mask_type     = p.mask_type;
            u.u_mask_strength = p.mask_strength;
            u.u_mask_pitch_px = p.mask_pitch_px;
            break;
        }
        case 5: { // Pass 4 bloom
            auto& u = *static_cast<PassUniforms_Pass4*>(out_buffer);
            u = {};
            u.u_resolution[0] = res_x; u.u_resolution[1] = res_y;
            u.u_bloom_strength    = p.bloom_strength;
            u.u_halation_strength = p.halation_strength;
            break;
        }
        case 6: { // Pass 5 temporal
            auto& u = *static_cast<PassUniforms_Pass5*>(out_buffer);
            u = {};
            u.u_persistence[0] = p.persistence_strength * p.persistence_ratio_r;
            u.u_persistence[1] = p.persistence_strength * p.persistence_ratio_g;
            u.u_persistence[2] = p.persistence_strength * p.persistence_ratio_b;
            u.u_history_valid  = history_valid ? 1 : 0;
            break;
        }
        case 7: { // Pass 6 composition
            auto& u = *static_cast<PassUniforms_Pass6*>(out_buffer);
            u = {};
            u.u_resolution[0]      = res_x; u.u_resolution[1] = res_y;
            u.u_barrel_strength    = p.barrel_strength;
            u.u_vignette_strength  = p.vignette_strength;
            u.u_gamma_display      = p.gamma_display;
            u.u_time               = time;
            u.u_warmup             = std::min(1.0f, time / 180.0f);
            u.u_monochrome         = p.monochrome;
            u.u_posterize_levels   = p.posterize_levels;
            u.u_glass_age          = p.glass_age;
            u.u_target_aspect      = p.target_aspect;
            u.u_bezel_style        = p.bezel_style;
            u.u_phosphor_color[0]  = p.phosphor_color_r;
            u.u_phosphor_color[1]  = p.phosphor_color_g;
            u.u_phosphor_color[2]  = p.phosphor_color_b;
            // u_has_bezel_image is patched by the caller after this fn,
            // because it depends on bezel_image_loaded_ pipeline state.
            u.u_has_bezel_image    = 0;
            u.u_glass_tint[0]      = p.glass_tint_r;
            u.u_glass_tint[1]      = p.glass_tint_g;
            u.u_glass_tint[2]      = p.glass_tint_b;
            break;
        }
        default:
            break;
    }
}

} // namespace

bool Pipeline::create(int output_width, int output_height) {
    output_width_  = output_width;
    output_height_ = output_height;

    // Lazily instantiate the default OpenGL backend if the caller did not
    // inject one. Phase 3b lets main.cpp pick D3D12 here.
    if (!backend_) {
        backend_ = create_backend(BackendKind::OpenGL);
        if (!backend_) {
            std::fprintf(stderr, "[tubelight] create_backend(OpenGL) returned null\n");
            return false;
        }
    }
    // Pipeline only knows about the framebuffer dimensions; main.cpp is
    // responsible for any pre-init() the backend needs (e.g. binding the
    // HWND for D3D12 before passing the backend to Pipeline). The GL
    // backend ignores the params struct.
    BackendInitParams bp;
    bp.width  = output_width;
    bp.height = output_height;
    if (!backend_->init(bp)) {
        std::fprintf(stderr, "[tubelight] backend init failed (%s)\n", backend_->name());
        return false;
    }
    if (!backend_->supports_pipeline()) {
        std::fprintf(stderr,
            "[tubelight] backend '%s' cannot drive Pipeline yet (Phase 3c pending).\n"
            "            Falling back is the caller's responsibility.\n",
            backend_->name());
        return false;
    }

    for (int i = 0; i < kPassCount; ++i) {
        enabled_[static_cast<size_t>(i)] = true;
    }
    if (!create_passes()) return false;

    // Pass 5 history: a render target whose color attachment matches the
    // pipeline intermediate format, plus a sampleable texture that the
    // backend copies into via copy_rt_to_texture(). Two handles because
    // GL conflates them (FBO color attachment IS the texture) but D3D12
    // needs separate resources with explicit transitions.
    history_rt_  = backend_->create_render_target(output_width, output_height,
                                                   PixelFormat::RGBA16_FLOAT);
    TextureDesc htd;
    htd.width  = output_width;
    htd.height = output_height;
    htd.format = PixelFormat::RGBA16_FLOAT;
    history_tex_ = backend_->create_texture(htd);
    if (!history_rt_.is_valid() || !history_tex_.is_valid()) {
        std::fprintf(stderr, "[tubelight] history RT/tex create failed\n");
        return false;
    }
    history_valid_ = false;

    return true;
}

bool Pipeline::create_passes() {
    for (int i = 0; i < kPassCount; ++i) {
        PassDesc pd;
        pd.pass_index          = i;
        pd.uniform_block_bytes = pass_uniforms_size(i);
        pd.texture_slot_count  = pass_texture_slot_count(i);
        pass_handles_[static_cast<size_t>(i)] = backend_->create_pass(pd);
        if (!pass_handles_[static_cast<size_t>(i)].is_valid()) {
            std::fprintf(stderr, "[tubelight] create_pass failed for pass %d\n", i);
            return false;
        }

        rt_handles_[static_cast<size_t>(i)] = backend_->create_render_target(
            output_width_, output_height_, PixelFormat::RGBA16_FLOAT);
        if (!rt_handles_[static_cast<size_t>(i)].is_valid()) {
            std::fprintf(stderr, "[tubelight] create_render_target failed for pass %d\n", i);
            return false;
        }
        rt_as_tex_[static_cast<size_t>(i)] = backend_->rt_as_texture(
            rt_handles_[static_cast<size_t>(i)]);
    }
    return true;
}

bool Pipeline::load_bezel_image(const std::string& path) {
    // File I/O still goes through Texture2D since stb_image lives there.
    // We then re-upload the pixels into a backend-owned TextureHandle so
    // bind_texture(slot, bezel_tex_) can route through the abstraction.
    clear_bezel_image();
    Texture2D file_tex;
    if (!file_tex.load_from_file(path, /*flip_vertical=*/true)) {
        std::fprintf(stderr, "[overlay] bezel image load failed: %s\n", path.c_str());
        return false;
    }
    bezel_w_ = file_tex.width();
    bezel_h_ = file_tex.height();
    // GL backend's path: pull the raw bytes back via glGetTexImage and
    // re-upload into the handle. Slow but only runs at load time.
    std::vector<uint8_t> pixels(static_cast<size_t>(bezel_w_) * bezel_h_ * 4);
    glBindTexture(GL_TEXTURE_2D, file_tex.id());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    TextureDesc td;
    td.width  = bezel_w_;
    td.height = bezel_h_;
    td.format = PixelFormat::RGBA8_UNORM;
    bezel_tex_ = backend_->create_texture(td);
    if (!bezel_tex_.is_valid() ||
        !backend_->upload_texture_rgba8(bezel_tex_, pixels.data(), bezel_w_, bezel_h_)) {
        std::fprintf(stderr, "[overlay] bezel handle upload failed\n");
        bezel_tex_ = {0};
        return false;
    }
    bezel_image_loaded_ = true;
    std::fprintf(stderr, "[overlay] bezel image loaded: %s (%dx%d)\n",
                 path.c_str(), bezel_w_, bezel_h_);
    return true;
}

void Pipeline::clear_bezel_image() {
    if (bezel_tex_.is_valid() && backend_) {
        backend_->destroy_texture(bezel_tex_);
    }
    bezel_tex_ = {0};
    bezel_image_loaded_ = false;
    bezel_w_ = bezel_h_ = 0;
}

void Pipeline::resize(int width, int height) {
    if (width == output_width_ && height == output_height_) return;
    output_width_  = width;
    output_height_ = height;
    if (!backend_) return;

    // Recreate the RTs at the new size. Pass handles are size-independent
    // (shaders + PSO state only) — keep them.
    for (int i = 0; i < kPassCount; ++i) {
        if (rt_as_tex_[static_cast<size_t>(i)].is_valid()) {
            backend_->destroy_texture(rt_as_tex_[static_cast<size_t>(i)]);
            rt_as_tex_[static_cast<size_t>(i)] = {0};
        }
        if (rt_handles_[static_cast<size_t>(i)].is_valid()) {
            backend_->destroy_render_target(rt_handles_[static_cast<size_t>(i)]);
        }
        rt_handles_[static_cast<size_t>(i)] = backend_->create_render_target(
            width, height, PixelFormat::RGBA16_FLOAT);
        rt_as_tex_[static_cast<size_t>(i)] = backend_->rt_as_texture(
            rt_handles_[static_cast<size_t>(i)]);
    }
    if (history_rt_.is_valid())  backend_->destroy_render_target(history_rt_);
    if (history_tex_.is_valid()) backend_->destroy_texture(history_tex_);
    history_rt_ = backend_->create_render_target(width, height, PixelFormat::RGBA16_FLOAT);
    TextureDesc htd; htd.width = width; htd.height = height; htd.format = PixelFormat::RGBA16_FLOAT;
    history_tex_ = backend_->create_texture(htd);
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

bool Pipeline::render_to_screen(uint32_t source_tex) {
    // Legacy GL-only entry point. Wrap the raw id and delegate to the
    // handle-based overload. Borrowed handle is invalidated at the end.
    if (!backend_) return false;
    // Width/height are informational; GL bind doesn't read them.
    TextureHandle borrowed = backend_->wrap_external_gl_texture(
        source_tex, output_width_, output_height_);
    if (!borrowed.is_valid()) {
        std::fprintf(stderr,
            "[tubelight] render_to_screen(uint32_t) needs a GL backend; "
            "use the TextureHandle overload with D3D12.\n");
        return false;
    }
    const bool ok = render_to_screen(borrowed);
    backend_->destroy_texture(borrowed);
    return ok;
}

bool Pipeline::render_to_screen(TextureHandle source) {
    if (output_width_ <= 0 || output_height_ <= 0 || !backend_) {
        return false;
    }

    // The "current input" for slot 0 starts as the user's source texture
    // and becomes the previous RT's color attachment for cascading passes.
    TextureHandle current_input = source;

    // ADR-0002 Phase 2c: skip pass 5 (i==6) entirely when the user-
    // selected persistence is sub-threshold for all three channels.
    const float kPersistenceEps = 1e-3f;
    const float persistence_total =
        params_.persistence_strength * (params_.persistence_ratio_r
                                       + params_.persistence_ratio_g
                                       + params_.persistence_ratio_b);
    const bool skip_pass5 = persistence_total < kPersistenceEps;

    for (int i = 0; i < kPassCount; ++i) {
        if (!enabled_[static_cast<size_t>(i)] || (i == 6 && skip_pass5)) {
            if (i == 6) history_valid_ = false;
            continue;
        }

        const bool is_last = (i == kPassCount - 1);
        if (is_last) {
            backend_->bind_default_framebuffer();
            backend_->set_viewport(0, 0, output_width_, output_height_);
        } else {
            backend_->bind_render_target(rt_handles_[static_cast<size_t>(i)]);
            backend_->set_viewport(0, 0, output_width_, output_height_);
        }

        backend_->clear_color(0.0f, 0.0f, 0.0f, 1.0f);
        backend_->bind_pass(pass_handles_[static_cast<size_t>(i)]);

        // --- Slot 0: u_source ---
        backend_->bind_texture(0, current_input);

        // --- Slot 1: pass-specific secondary input ---
        if (i == 6) {
            // Pass 5: u_prev_frame from history snapshot. The
            // `u_history_valid` flag in the uniform block handles the
            // first-frame / disabled case.
            backend_->bind_texture(1, history_tex_);
        } else if (i == 7) {
            // Pass 6: u_bezel_tex (only when an image was loaded).
            if (bezel_image_loaded_ && bezel_tex_.is_valid()) {
                backend_->bind_texture(1, bezel_tex_);
            }
        }

        // --- Uniforms ---
        // build_uniforms_for_pass writes into the largest-known POD so
        // any pass index fits without per-case storage. Then we send
        // exactly sizeof(matching struct) so the backend's size assert
        // passes.
        alignas(16) uint8_t uniform_buf[sizeof(PassUniforms_Pass6)];
        std::memset(uniform_buf, 0, sizeof(uniform_buf));
        build_uniforms_for_pass(i, params_, output_width_, output_height_,
                                 time_, frame_mean_lum_,
                                 history_valid_, signal_snapshot_,
                                 uniform_buf);

        // Pass 6's u_has_bezel_image isn't a function of the snapshot
        // we built — it depends on pipeline state. Patch it now.
        if (i == 7) {
            auto* u6 = reinterpret_cast<PassUniforms_Pass6*>(uniform_buf);
            u6->u_has_bezel_image = bezel_image_loaded_ ? 1 : 0;
        }

        backend_->set_uniform_block(pass_handles_[static_cast<size_t>(i)],
                                     uniform_buf, pass_uniforms_size(i));

        backend_->draw_fullscreen_quad();

        // After pass 5 has rendered into rt_handles_[6], snapshot the RT
        // into history_tex_ so the next frame's pass 5 can sample it.
        if (i == 6) {
            backend_->copy_rt_to_texture(rt_handles_[static_cast<size_t>(i)],
                                          history_tex_);
            history_valid_ = true;
        }

        if (!is_last) {
            // Next pass samples this pass's output. Cached at
            // create_passes() so we don't allocate per-frame.
            current_input = rt_as_tex_[static_cast<size_t>(i)];
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

    // "basic" preset — the clean out-of-box default: aperture-grille grid +
    // soft scanlines and NOTHING else. Zero every extra effect that isn't a
    // CRTProfile field (these otherwise keep the GlobalParams defaults), and
    // leave target_aspect at 0 so it never changes the window's form factor.
    if (p.id == "basic") {
        params_.bloom_strength       = 0.0f;
        params_.halation_strength    = 0.0f;
        params_.barrel_strength      = 0.0f;
        params_.vignette_strength    = 0.0f;
        params_.persistence_strength = 0.0f;  // no trails when dragging
        params_.target_aspect        = 0.0f;  // fill — keep the form factor
        params_.bezel_style          = 0;
        // Reset colour/mono fields too (in case we switched from a mono tube).
        params_.monochrome       = 0;
        params_.posterize_levels = 0;
        params_.phosphor_color_r = params_.phosphor_color_g = params_.phosphor_color_b = 1.0f;
        params_.glass_tint_r     = params_.glass_tint_g     = params_.glass_tint_b     = 1.0f;
        params_.glass_age        = 0.0f;
        return;  // skip the per-tube tuning below; basic stays minimal
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
        // Default posterize: OFF for terminals (real terminals had an
        // analog continuous-tone phosphor, not discrete steps — banding
        // was a previous-session bug that made gradients look digital).
        // Mac-Classic-class profiles override to 2 below for true 1-bit.
        params_.posterize_levels = 0;

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
                // P31 chromaticity ≈ (0.260, 0.530) — yellow-green. The
                // colour the user actually saw on a real tube was MUCH
                // more saturated than the (1931 CIE xy) chromaticity
                // suggests once you account for the dark surround and
                // photopic sensitivity peak. Values boosted from the
                // washed-out (0.30, 1.05, 0.40) per user feedback.
                params_.phosphor_color_r = 0.12f;
                params_.phosphor_color_g = 1.30f;
                params_.phosphor_color_b = 0.18f;
                break;
            case PhosphorType::P3:  // amber (IBM 5151 / HP)
                // P3 chromaticity ≈ (0.485, 0.490) — orange-amber. The
                // visible look is closer to a deep amber/orange than
                // the soft pastel the previous (1.05, 0.70, 0.15) gave.
                params_.phosphor_color_r = 1.30f;
                params_.phosphor_color_g = 0.55f;
                params_.phosphor_color_b = 0.05f;
                break;
            case PhosphorType::P1:  // deep green oscilloscope tube
                // Deeper, more saturated green than P31 — oscope tubes
                // were tuned for long persistence + visibility on a
                // bright lab room. Push green higher, red lower.
                params_.phosphor_color_r = 0.05f;
                params_.phosphor_color_g = 1.35f;
                params_.phosphor_color_b = 0.20f;
                break;
            case PhosphorType::P4:  // B&W: analog TV by default → no posterize.
                // P4 is a cool-white phosphor with a slight blue cast
                // on real tubes (visible in B&W TV photographs).
                params_.phosphor_color_r = 0.92f;
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

    // Bezels disabled by default — the SDF bezels did not satisfy
    // expectations and there are no PNG assets shipping yet. User can
    // opt in via the menu Composition → "Bezel style" combo, or by
    // dropping an `assets/bezels/<id>.png` file (which triggers PNG
    // mode independently of bezel_style).
    params_.bezel_style = 0;

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

        // Per-profile fine-tuning: each colour CRT has its own
        // characteristic look. The JSON profile only configures a
        // subset (scanline_strength, mask_type, glass tint), so we
        // tune the rest here by id. Comments cite source-of-truth.
        //   PVM/BVM   = pro broadcast, sharp scanlines, modest bloom
        //   FW900     = HD widescreen, less scanline aggression
        //   1084S     = consumer shadow mask, softer/warmer
        //   X68K      = JP pro monitor, PVM-like
        //   MultiSync = 1990s VGA, fine scanlines (350+ raster)
        //   K7000     = arcade tube, gritty mask, strong bloom
        const std::string& id = p.id;
        if (id.find("bvm") != std::string::npos) {
            // BVM is even sharper / cleaner than PVM.
            params_.scanline_strength = 0.55f;
            params_.beam_width        = 1.10f;
            params_.mask_strength     = 0.30f;
            params_.bloom_strength    = 0.10f;
            params_.halation_strength = 0.08f;
            params_.gamma_crt         = 2.5f;
            params_.scanline_count    = 240.0f;
            params_.persistence_strength = 0.28f;
        } else if (id.find("fw900") != std::string::npos) {
            params_.scanline_strength = 0.30f;  // HD doesn't need heavy lines
            params_.beam_width        = 1.20f;
            params_.mask_strength     = 0.25f;
            params_.bloom_strength    = 0.14f;
            params_.halation_strength = 0.10f;
            params_.gamma_crt         = 2.4f;
            params_.scanline_count    = 480.0f;  // higher native res
            params_.persistence_strength = 0.25f;
        } else if (id.find("pvm") != std::string::npos) {
            params_.scanline_strength = 0.65f;  // PVM signature crisp lines
            params_.beam_width        = 1.20f;
            params_.mask_strength     = 0.42f;
            params_.bloom_strength    = 0.14f;
            params_.halation_strength = 0.10f;
            params_.gamma_crt         = 2.5f;
            params_.scanline_count    = 240.0f;
            params_.persistence_strength = 0.32f;
        } else if (id.find("commodore-1084") != std::string::npos) {
            // Consumer shadow mask: softer, warmer, more bloom.
            params_.scanline_strength = 0.50f;
            params_.beam_width        = 1.45f;
            params_.mask_strength     = 0.35f;
            params_.bloom_strength    = 0.22f;
            params_.halation_strength = 0.15f;
            params_.gamma_crt         = 2.4f;
            params_.scanline_count    = 288.0f;  // PAL
            params_.persistence_strength = 0.38f;
        } else if (id.find("sharp") != std::string::npos ||
                   id.find("x68k")  != std::string::npos) {
            // X68K monitors: JP pro, behaves PVM-class.
            params_.scanline_strength = 0.60f;
            params_.beam_width        = 1.25f;
            params_.mask_strength     = 0.40f;
            params_.bloom_strength    = 0.15f;
            params_.halation_strength = 0.10f;
            params_.gamma_crt         = 2.5f;
            params_.scanline_count    = 256.0f;
            params_.persistence_strength = 0.30f;
        } else if (id.find("multisync") != std::string::npos ||
                   id.find("nec")       != std::string::npos) {
            // 1990s VGA-era monitors: dense raster, milder scanlines.
            params_.scanline_strength = 0.35f;
            params_.beam_width        = 1.30f;
            params_.mask_strength     = 0.30f;
            params_.bloom_strength    = 0.16f;
            params_.halation_strength = 0.08f;
            params_.gamma_crt         = 2.3f;
            params_.scanline_count    = 480.0f;
            params_.persistence_strength = 0.25f;
        } else if (id.find("wells")   != std::string::npos ||
                   id.find("k7000")   != std::string::npos) {
            // Arcade tube: gritty look.
            params_.scanline_strength = 0.75f;
            params_.beam_width        = 1.50f;
            params_.mask_strength     = 0.55f;
            params_.bloom_strength    = 0.30f;
            params_.halation_strength = 0.20f;
            params_.gamma_crt         = 2.6f;
            params_.scanline_count    = 240.0f;
            params_.persistence_strength = 0.40f;
        }
        // Generic / unknown profiles keep the defaults set above.
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
