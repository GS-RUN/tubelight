// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)

#pragma once

#include "core/quad.h"
#include "profile/crt_profile.h"
#include "profile/signal_profile.h"
#include "render/backend.h"
#include "render/handle.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace tubelight {

// Pipeline of 8 passes (indices 0..7 map to Pass −1, 0, 1, 2, 3, 4, 5, 6).
// Each pass reads from the previous pass's FBO color attachment and writes
// to its own FBO. The final pass can target the default framebuffer.
//
// Layout (see specs/DESIGN.md §D1):
//   index 0 (Pass −1) — signal modeling
//   index 1 (Pass  0) — analysis (dithering detection, luminance avg)
//   index 2 (Pass  1) — dithering reconstruction
//   index 3 (Pass  2) — beam + scanlines + linearization gamma 2.5
//   index 4 (Pass  3) — shadow mask 3D
//   index 5 (Pass  4) — bloom + halation
//   index 6 (Pass  5) — temporal (persistence + voltage bloom + BFI)
//   index 7 (Pass  6) — composition (barrel, vignette, gamma encode)
//
// In F2 several passes are identity stubs (the proper implementation lands
// in F4/F5/F7 per PLAN). All 8 exist from day 1 so the pipeline structure
// is fixed.
class Pipeline {
public:
    static constexpr int kPassCount = 8;

    Pipeline() = default;
    ~Pipeline() = default;

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    // Inject a render backend. Must be called BEFORE create(); if not set,
    // create() instantiates a default OpenGL backend. Takes ownership.
    void set_backend(std::unique_ptr<IRenderBackend> backend) {
        backend_ = std::move(backend);
    }

    // Identifier of the current backend (for diagnostics / menu). Returns
    // "(none)" when no backend has been created/injected yet.
    const char* backend_name() const {
        return backend_ ? backend_->name() : "(none)";
    }

    // Raw access to the active backend (for the --bench harness: frame
    // timing + finish). Null until create()/set_backend(). Non-owning.
    IRenderBackend* backend() const { return backend_.get(); }

    bool create(int output_width, int output_height);
    void resize(int width, int height);

    // Applies all enabled passes to a source texture and renders the
    // result to the currently bound default framebuffer (the window).
    // Returns false only if the pipeline is unusable.
    //
    // Two overloads:
    //   render_to_screen(TextureHandle) — the path Phase 3c+ targets;
    //     works with both GL and D3D12 backends.
    //   render_to_screen(uint32_t)      — GL-only legacy entry point;
    //     keeps overlay_mode_win.cpp (DXGI capture loop) working until
    //     it gets migrated. GL backend wraps the raw id into an internal
    //     borrowed handle. D3D12 backend rejects this overload.
    bool render_to_screen(TextureHandle source);
    bool render_to_screen(uint32_t source_tex);

    // Toggles a single pass. Disabled passes act as identity (pass-through).
    void set_pass_enabled(int pass_index, bool enabled);
    bool is_pass_enabled(int pass_index) const;

    // Master uniforms applied to every pass (each shader picks the ones it uses).
    // Defaults are tuned conservatively for *desktop overlay* use — visible
    // CRT character without overwhelming icons / UI. Retro emulation users
    // who want the full PVM look should bump them up via the runtime menu
    // (Ctrl+Alt+M) or by adjusting their CRTProfile JSON.
    struct GlobalParams {
        // Pass 2 (beam + scanlines)
        float scanline_strength = 0.35f;
        float beam_width        = 1.30f;
        // CRT gamma 2.2 matches sRGB so passthrough mode round-trips
        // cleanly. Old default of 2.5 paired with display gamma 2.2
        // crushed midtones (~src^1.14) and "ate" subtle background
        // detail in monochrome / dark scenes. The user can still crank
        // it up via the slider for a more contrasty look.
        float gamma_crt         = 2.2f;
        // Visible scanlines per output frame: 240 = NTSC raster visible,
        // 288 = PAL, 480 = VGA, etc. Set from SignalProfile when available;
        // a slider in the menu lets the user pick non-standard values.
        float scanline_count    = 240.0f;

        // Pass 3 (mask)
        int   mask_type         = 2;     // 0=none 1=shadow 2=aperture 3=slot 4=diamond 5=cgwg 6=dottrio
        float mask_strength     = 0.22f;
        float mask_pitch_px     = 3.0f;

        // Pass 4 (bloom + halation)
        float bloom_strength    = 0.18f;
        float halation_strength = 0.12f;

        // Pass 6 (composition)
        float barrel_strength   = 0.02f;
        float vignette_strength = 0.12f;
        float gamma_display     = 2.2f;
        // Target aspect ratio (width/height): 0 = fill window, 1.333 = 4:3,
        // 1.25 = 5:4, 1.778 = 16:9. When non-zero and the window aspect
        // differs, the shader letterboxes / pillarboxes with black bars.
        float target_aspect     = 0.0f;

        // Programmatic bezel (Pass 6 SDF, no PNG asset).
        // 0 = none (plain black bars), 1 = pvm metal black,
        // 2 = beige plastic terminal, 3 = wood-tone B&W TV console,
        // 4 = compact Mac (asymmetric white plastic), 5 = generic dark.
        int   bezel_style       = 0;

        // Pass 5 (temporal phosphor persistence)
        // Per-channel exponential decay. persistence_strength is a global
        // 0..1 dial; per-channel ratios modulate it (P22 colour CRTs have
        // a slower red than green/blue → ratio_r=1.0, ratio_g=ratio_b=0.5
        // gives the classic warm trail on bright moving content).
        // Effective per-channel decay = strength * ratio, clamped to [0..1).
        float persistence_strength = 0.0f;
        float persistence_ratio_r  = 1.0f;
        float persistence_ratio_g  = 0.5f;
        float persistence_ratio_b  = 0.5f;

        // Phosphor / glass tint (Pass 6 — from CRTProfile).
        // monochrome=1 collapses input to luminance and recolours through
        // phosphor_color (P31 green, P3 amber, P4 cool-white...). glass_tint
        // is a per-channel multiplier; glass_age adds amber drift on top.
        // posterize_levels > 1 quantises the monochrome luminance into N
        // discrete steps — 2 = pure 1-bit (Mac Classic), 4..8 = early
        // text-terminal feel, 0 = full analog (B&W TV).
        int   monochrome        = 0;
        int   posterize_levels  = 0;
        float phosphor_color_r  = 1.0f;
        float phosphor_color_g  = 1.0f;
        float phosphor_color_b  = 1.0f;
        float glass_tint_r      = 1.0f;
        float glass_tint_g      = 1.0f;
        float glass_tint_b      = 1.0f;
        float glass_age         = 0.0f;
    };

    GlobalParams& params() { return params_; }
    const GlobalParams& params() const { return params_; }

    // Profile-driven parameter application. Calling these overwrites the
    // relevant subset of GlobalParams from the profile JSON values.
    void apply_crt_profile(const CRTProfile& p);
    void apply_signal_profile(const SignalProfile& p);

    // Per-frame time uniform fed to Pass −1 noise.
    void set_time(float t) { time_ = t; }
    float time() const { return time_; }

    // Cheap CPU-side rolling estimate of average frame luminance (0..1).
    // Drives voltage-bloom in Pass 2: a brighter screen sags the CRT
    // power supply, the beam current goes up, and the beam widens.
    void set_frame_mean_luminance(float lum) { frame_mean_lum_ = lum; }
    float frame_mean_luminance() const { return frame_mean_lum_; }

    // Optional signal profile snapshot used to feed Pass −1 uniforms.
    void set_signal_profile_snapshot(const SignalProfile& s) { signal_snapshot_ = s; }
    const std::optional<SignalProfile>& signal_profile_snapshot() const { return signal_snapshot_; }

    // Read-only access for introspection — kept for any external code
    // that wanted to peek; now returns handle ids (0 if not yet created).
    uint32_t pass_handle_id(int pass_index) const {
        return pass_handles_[static_cast<size_t>(pass_index)].id;
    }
    uint32_t render_target_id(int pass_index) const {
        return rt_handles_[static_cast<size_t>(pass_index)].id;
    }

private:
    bool create_passes();

    int output_width_  = 0;
    int output_height_ = 0;

    std::array<PassHandle, kPassCount>         pass_handles_;
    std::array<RenderTargetHandle, kPassCount> rt_handles_;
    // Borrowed texture handles wrapping each RT's color attachment.
    // Cached at create_passes() so the per-frame cascade doesn't allocate
    // a new entry every pass × frame (would leak into the backend's pool).
    std::array<TextureHandle, kPassCount>      rt_as_tex_;
    std::array<bool, kPassCount>               enabled_;

    GlobalParams params_;
    // Render backend (Phase 3a). Owns the fullscreen-triangle VAO and
    // — since Phase 3c — every GPU resource used by Pipeline (passes,
    // render targets, textures, history snapshot). Created in create()
    // if not pre-injected via set_backend().
    std::unique_ptr<IRenderBackend> backend_;
    float time_ = 0.0f;
    float frame_mean_lum_ = 0.0f;
    std::optional<SignalProfile> signal_snapshot_;

    // Pass 5 history: previous frame's pass-5 output, snapshotted into
    // history_tex_ so the next frame's temporal shader can sample it
    // for phosphor persistence. Marked invalid on resize / first frame.
    RenderTargetHandle history_rt_{0};   // intermediate RT used by snapshot
    TextureHandle      history_tex_{0};  // the snapshot sampled next frame
    bool               history_valid_ = false;

    // Optional per-profile bezel PNG sampled by pass 6. NULL handle when
    // no image is loaded (the shader falls back to the procedural SDF
    // bezel — see u_has_bezel_image uniform).
    TextureHandle bezel_tex_{0};
    int           bezel_w_ = 0;
    int           bezel_h_ = 0;
    bool          bezel_image_loaded_ = false;

public:
    bool load_bezel_image(const std::string& path);
    void clear_bezel_image();
    bool has_bezel_image() const { return bezel_image_loaded_; }
};

const char* pass_display_name(int pass_index);

} // namespace tubelight
