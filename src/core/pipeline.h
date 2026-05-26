// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN

#pragma once

#include "core/fbo.h"
#include "core/quad.h"
#include "core/shader.h"

#include <array>
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

    bool create(int output_width, int output_height);
    void resize(int width, int height);

    // Applies all enabled passes to source_tex and renders the result to the
    // currently bound default framebuffer (i.e. the window). Returns false
    // only if the pipeline is unusable.
    bool render_to_screen(GLuint source_tex);

    // Toggles a single pass. Disabled passes act as identity (pass-through).
    void set_pass_enabled(int pass_index, bool enabled);
    bool is_pass_enabled(int pass_index) const;

    // Master uniforms applied to every pass (each shader picks the ones it uses).
    struct GlobalParams {
        // Pass 2 (beam + scanlines)
        float scanline_strength = 0.75f;
        float beam_width        = 1.30f;
        float gamma_crt         = 2.5f;

        // Pass 3 (mask)
        int   mask_type         = 1;     // 0=none 1=shadow 2=aperture 3=slot 4=diamond 5=cgwg 6=dottrio
        float mask_strength     = 0.55f;
        float mask_pitch_px     = 3.0f;  // approximate pixel pitch of the mask cell

        // Pass 4 (bloom + halation)
        float bloom_strength    = 0.50f;
        float halation_strength = 0.35f;

        // Pass 6 (composition)
        float barrel_strength   = 0.06f;
        float vignette_strength = 0.35f;
        float gamma_display     = 2.2f;
    };

    GlobalParams& params() { return params_; }
    const GlobalParams& params() const { return params_; }

    // Read-only access for introspection / tests.
    const FBO& fbo(int pass_index) const { return fbos_[static_cast<size_t>(pass_index)]; }
    const ShaderProgram& shader(int pass_index) const { return shaders_[static_cast<size_t>(pass_index)]; }

private:
    bool load_shader(int pass_index, const std::string& filename);
    bool reload_all_shaders();

    static constexpr const char* kPassFilenames[kPassCount] = {
        "pass_minus1_signal.frag",
        "pass0_analysis.frag",
        "pass1_dither_reconstruct.frag",
        "pass2_beam_scanlines.frag",
        "pass3_mask.frag",
        "pass4_bloom.frag",
        "pass5_temporal.frag",
        "pass6_composition.frag",
    };

    int output_width_  = 0;
    int output_height_ = 0;

    std::array<ShaderProgram, kPassCount> shaders_;
    std::array<FBO, kPassCount> fbos_;
    std::array<bool, kPassCount> enabled_;

    GlobalParams params_;
    FullscreenQuad quad_;
};

const char* pass_display_name(int pass_index);

} // namespace tubelight
