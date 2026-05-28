// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)
//
// Phase 3c of ADR-0002 — per-pass uniform POD structs.
//
// Each struct mirrors the std140 uniform block in the matching
// shaders/pass*.frag. Backend implementations consume them as opaque
// byte arrays via `IRenderBackend::set_uniform_block(handle, data, size)`:
//
//   - GLBackend memcpy's into a CPU buffer and replays as
//     glUniform*() per field at draw time.
//   - D3D12Backend memcpy's into a constant buffer mapped from an
//     UPLOAD heap; the cbuffer layout in HLSL matches std140 byte-
//     for-byte (SPIRV-Cross emits packoffsets that match the GLSL
//     std140 packing of the source block).
//
// Constitution C3c-2 ("pixel-equivalence gate cuantitativo") and risk
// R3c-3 ("Constant buffer layout drift entre C++ y HLSL") force the
// strict size assertions below. Any drift between the GLSL block, the
// C++ POD, or the dxc cbuffer reflection triggers a build failure /
// runtime assert before it corrupts a single pixel.
//
// The expected size is hand-computed from the std140 layout declared in
// each shader. Cross-checked at build time against the HLSL output:
//   for f in build/.../shaders/hlsl/pass*.hlsl; do
//     grep -A 30 '^cbuffer' "$f" | tail -1
//   done
// gives the offset of the last field; size = offset + sizeof(last_field)
// rounded up to 16-byte multiple.

#pragma once

#include <cstddef>
#include <cstdint>

namespace tubelight {

#pragma pack(push, 16)

// Pass -1 — signal modeling (NTSC artifacts, noise, ringing, ghosting).
// Matches shaders/pass_minus1_signal.frag std140 block. 64 bytes.
struct PassUniforms_PassMinus1 {
    float u_resolution[2];          // offset 0,  size 8
    float u_luma_mhz;               // offset 8
    float u_chroma_i_mhz;           // offset 12
    float u_chroma_q_mhz;           // offset 16
    float u_dot_crawl_strength;     // offset 20
    float u_rainbow_banding;        // offset 24
    float u_ringing_amount;         // offset 28
    float u_ghosting_offset_px;     // offset 32
    int32_t u_noise_type;           // offset 36
    float u_noise_strength;         // offset 40
    int32_t u_signal_connection;    // offset 44
    float u_time;                   // offset 48
    float _pad0;                    // offset 52
    float _pad1;                    // offset 56
    float _pad2;                    // offset 60 — total 64
};
static_assert(sizeof(PassUniforms_PassMinus1) == 64);
static_assert(sizeof(PassUniforms_PassMinus1) % 16 == 0);

// Pass 0 — dither detection (alpha mask output). 16 bytes.
struct PassUniforms_Pass0 {
    float u_resolution[2];           // offset 0
    float u_dither_detect_threshold; // offset 8
    float _pad0;                     // offset 12 — total 16
};
static_assert(sizeof(PassUniforms_Pass0) == 16);

// Pass 1 — dither reconstruction. 16 bytes.
struct PassUniforms_Pass1 {
    float u_resolution[2];             // offset 0
    float u_reconstruction_strength;   // offset 8
    float _pad0;                       // offset 12 — total 16
};
static_assert(sizeof(PassUniforms_Pass1) == 16);

// Pass 2 — beam + scanlines + CRT-gamma linearization. 32 bytes.
struct PassUniforms_Pass2 {
    float u_resolution[2];      // offset 0
    float u_scanline_strength;  // offset 8
    float u_beam_width;         // offset 12
    float u_gamma_crt;          // offset 16
    float u_scanline_count;     // offset 20
    float u_frame_mean_lum;     // offset 24
    float _pad0;                // offset 28 — total 32
};
static_assert(sizeof(PassUniforms_Pass2) == 32);

// Pass 3 — shadow mask / aperture grille / etc. 32 bytes.
struct PassUniforms_Pass3 {
    float u_resolution[2];   // offset 0
    int32_t u_mask_type;     // offset 8
    float u_mask_strength;   // offset 12
    float u_mask_pitch_px;   // offset 16
    float _pad0;             // offset 20
    float _pad1;             // offset 24
    float _pad2;             // offset 28 — total 32
};
static_assert(sizeof(PassUniforms_Pass3) == 32);

// Pass 4 — bloom + halation. 16 bytes.
struct PassUniforms_Pass4 {
    float u_resolution[2];      // offset 0
    float u_bloom_strength;     // offset 8
    float u_halation_strength;  // offset 12 — total 16
};
static_assert(sizeof(PassUniforms_Pass4) == 16);

// Pass 5 — temporal phosphor persistence. 16 bytes.
// std140 rule: vec3 occupies the first 12 B of its 16-aligned slot; the
// trailing int absorbs the last 4 B (no extra padding).
struct PassUniforms_Pass5 {
    float u_persistence[3];   // offset 0, size 12 (treated as vec3 by HLSL)
    int32_t u_history_valid;  // offset 12 — total 16
};
static_assert(sizeof(PassUniforms_Pass5) == 16);

// Pass 6 — composition: barrel + vignette + gamma + tint + phosphor color
// + bezel SDF. 80 bytes.
struct PassUniforms_Pass6 {
    float u_resolution[2];        // offset 0
    float u_barrel_strength;      // offset 8
    float u_vignette_strength;    // offset 12
    float u_gamma_display;        // offset 16
    float u_time;                 // offset 20
    float u_warmup;               // offset 24
    int32_t u_monochrome;         // offset 28
    int32_t u_posterize_levels;   // offset 32
    float u_glass_age;            // offset 36
    float u_target_aspect;        // offset 40
    int32_t u_bezel_style;        // offset 44
    float u_phosphor_color[3];    // offset 48, size 12 (vec3)
    int32_t u_has_bezel_image;    // offset 60
    float u_glass_tint[3];        // offset 64, size 12 (vec3)
    float _pad0;                  // offset 76 — total 80
};
static_assert(sizeof(PassUniforms_Pass6) == 80);
static_assert(sizeof(PassUniforms_Pass6) % 16 == 0);

#pragma pack(pop)

// Index-by-pass helper. Used by GLBackend to know the expected size of
// the uniform block for each pass at create_pass() time.
// Convention matches Pipeline::kPassFilenames order: 0..7 → Pass −1..6.
constexpr size_t pass_uniforms_size(int pass_index) {
    switch (pass_index) {
        case 0: return sizeof(PassUniforms_PassMinus1);
        case 1: return sizeof(PassUniforms_Pass0);
        case 2: return sizeof(PassUniforms_Pass1);
        case 3: return sizeof(PassUniforms_Pass2);
        case 4: return sizeof(PassUniforms_Pass3);
        case 5: return sizeof(PassUniforms_Pass4);
        case 6: return sizeof(PassUniforms_Pass5);
        case 7: return sizeof(PassUniforms_Pass6);
    }
    return 0;
}

// Number of texture slots each pass binds. Slot 0 is always the primary
// input (u_source). Slot 1 is u_prev_frame on pass 5, u_bezel_tex on
// pass 6. Convention pinned for the D3D12 root signature.
constexpr int pass_texture_slot_count(int pass_index) {
    if (pass_index == 6 || pass_index == 7) return 2; // pass 5 & pass 6
    return 1;
}

} // namespace tubelight
