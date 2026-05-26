// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// Pass -1 — Signal modeling: RF / Composite / S-Video / SCART-RGB / Component / VGA.
//
// Models the analog connection between the source's RGB framebuffer and the
// CRT tube. This is the layer that makes the Sonic Green Hill cascade fuse
// into water on composite and stay as bars on RGB.
//
// Pipeline:
//   1. Decode source RGB to YIQ (NTSC) or YUV (PAL).
//   2. Low-pass luma to u_luma_mhz cutoff.
//   3. Low-pass chroma channels to u_chroma_*_mhz cutoff (tighter than luma).
//   4. Optionally modulate dot crawl from chroma onto luma (subcarrier beat).
//   5. Ringing: high-freq overshoot at luma edges.
//   6. Ghosting: blended duplicate at +u_ghosting_offset_pixels.
//   7. Line-coherent noise for RF / Composite.
//   8. Re-encode to RGB and output.
//
// All bandwidth-derived radii are scaled by the source pixel rate. We treat
// each source pixel as one sample at the implied composite pixel clock
// (~5.37 MHz NTSC), so 1.0 MHz cutoff ≈ blur radius of ~5.37 samples.

#version 450 core

in vec2 v_uv;
out vec4 o_color;

uniform sampler2D u_source;
uniform vec2  u_resolution;

// Signal profile uniforms (mirror of SignalProfile JSON)
uniform float u_luma_mhz;            // e.g. 4.2 NTSC, 5.0 PAL, 25.0 VGA
uniform float u_chroma_i_mhz;
uniform float u_chroma_q_mhz;
uniform float u_dot_crawl_strength;  // [0..1]
uniform float u_rainbow_banding;     // [0..1]
uniform float u_ringing_amount;      // [0..1]
uniform float u_ghosting_offset_px;  // pixels
uniform int   u_noise_type;          // 0=pixel 1=line 2=rf 3=none
uniform float u_noise_strength;      // [0..1]
uniform int   u_signal_connection;   // 0=rf 1=composite 2=svideo 3=scart_rgb 4=component 5=rgb_vga

// Time for noise (set per-frame from CPU); 0 if no animation
uniform float u_time;

// ---- YIQ/RGB conversion (NTSC) ------------------------------------------
const mat3 kRGBtoYIQ = mat3(
    0.299,   0.587,   0.114,
    0.596,  -0.275,  -0.321,
    0.212,  -0.523,   0.311
);
const mat3 kYIQtoRGB = mat3(
    1.0,   0.956,   0.621,
    1.0,  -0.272,  -0.647,
    1.0,  -1.106,   1.703
);

vec3 rgb_to_yiq(vec3 c) { return c * kRGBtoYIQ; }
vec3 yiq_to_rgb(vec3 c) { return c * kYIQtoRGB; }

// ---- Pseudo-random for noise --------------------------------------------
float hash11(float x) {
    return fract(sin(x * 12.9898) * 43758.5453);
}
float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

// ---- Horizontal blur sampling channel `ch` of YIQ over `radius_px` ------
// Sigma matched to a Gaussian whose -3dB point ~ bandwidth limit.
float blur_y_at_offset(vec2 uv, float radius_px, vec2 px_size, int channel) {
    // 9-tap symmetric kernel; weights computed from a Gaussian sigma=radius_px/2.
    float sigma = max(radius_px * 0.5, 0.5);
    float sum_w = 0.0;
    float sum_v = 0.0;
    for (int i = -4; i <= 4; ++i) {
        float w = exp(-(float(i) * float(i)) / (2.0 * sigma * sigma));
        vec3 yiq = rgb_to_yiq(texture(u_source, uv + vec2(float(i) * px_size.x, 0.0)).rgb);
        sum_v += yiq[channel] * w;
        sum_w += w;
    }
    return sum_v / max(sum_w, 1e-4);
}

void main() {
    // RGB-VGA path: pure passthrough (no analog encoding stage).
    if (u_signal_connection == 5) {
        o_color = vec4(texture(u_source, v_uv).rgb, 0.0);
        return;
    }

    vec2 px_size = 1.0 / u_resolution;
    vec3 source_rgb = texture(u_source, v_uv).rgb;
    vec3 yiq = rgb_to_yiq(source_rgb);

    // ---- bandwidth limiting -------------------------------------------
    // Reference composite-NTSC pixel-clock: 4 * subcarrier = ~14.32 MHz total chroma+luma.
    // For consumer modeling we treat source pixels as one sample at ~5.37 MHz
    // (3 * subcarrier / 2). Convert MHz to blur radius in source-pixels:
    //   radius_px = max(1, reference_mhz / target_mhz)
    const float kReferenceMhz = 5.37;
    float radius_y = max(1.0, kReferenceMhz / max(u_luma_mhz,     1e-3));
    float radius_i = max(1.0, kReferenceMhz / max(u_chroma_i_mhz, 1e-3));
    float radius_q = max(1.0, kReferenceMhz / max(u_chroma_q_mhz, 1e-3));

    // SCART-RGB / Component: chroma carriers don't exist, so we skip chroma
    // blur entirely (the RGB or YPbPr channels are independent).
    bool has_modulated_chroma = (u_signal_connection == 0 || u_signal_connection == 1);
    bool has_separated_chroma = (u_signal_connection == 2); // S-Video

    // S-Video keeps chroma BW closer to spec, but still less than full luma
    // bandwidth, so we still blur — just less.
    float y_filtered = blur_y_at_offset(v_uv, radius_y, px_size, 0);
    float i_filtered = (has_modulated_chroma || has_separated_chroma)
                        ? blur_y_at_offset(v_uv, radius_i, px_size, 1) : yiq.y;
    float q_filtered = (has_modulated_chroma || has_separated_chroma)
                        ? blur_y_at_offset(v_uv, radius_q, px_size, 2) : yiq.z;

    vec3 yiq_filtered = vec3(y_filtered, i_filtered, q_filtered);

    // ---- dot crawl: subcarrier leakage into luma ----------------------
    if (has_modulated_chroma && u_dot_crawl_strength > 0.0) {
        // Beat between pixel rate and subcarrier produces 2-pixel checkerboard.
        float crawl_phase = mod(v_uv.x * u_resolution.x + v_uv.y * u_resolution.y, 2.0);
        float crawl = (crawl_phase < 1.0) ? 1.0 : -1.0;
        float chroma_mag = length(yiq_filtered.yz);
        yiq_filtered.x += crawl * chroma_mag * 0.15 * u_dot_crawl_strength;
    }

    // ---- ringing: edge overshoot --------------------------------------
    if (u_ringing_amount > 0.0) {
        float left  = rgb_to_yiq(texture(u_source, v_uv + vec2(-px_size.x, 0.0)).rgb).x;
        float right = rgb_to_yiq(texture(u_source, v_uv + vec2( px_size.x, 0.0)).rgb).x;
        float edge  = abs(right - left);
        float ring  = sin(v_uv.x * u_resolution.x * 6.28318) * edge * 0.3 * u_ringing_amount;
        yiq_filtered.x += ring;
    }

    // ---- ghosting (RF) -----------------------------------------------
    if (u_ghosting_offset_px > 0.0) {
        vec2 off = vec2(-u_ghosting_offset_px * px_size.x, 0.0);
        vec3 ghost_yiq = rgb_to_yiq(texture(u_source, v_uv + off).rgb);
        yiq_filtered = mix(yiq_filtered, ghost_yiq, 0.18);
    }

    // ---- noise --------------------------------------------------------
    if (u_noise_type != 3 && u_noise_strength > 0.0) {
        float n;
        if (u_noise_type == 0) {
            // per-pixel
            n = hash21(v_uv * u_resolution + vec2(u_time, 0.0)) - 0.5;
        } else if (u_noise_type == 1) {
            // per-line (one value across the whole scanline)
            n = hash11(v_uv.y * u_resolution.y + u_time) - 0.5;
        } else {
            // rf: low-freq line + high-freq pixel
            float line  = hash11(v_uv.y * u_resolution.y + u_time) - 0.5;
            float pixel = hash21(v_uv * u_resolution + vec2(u_time, 0.0)) - 0.5;
            n = line * 0.7 + pixel * 0.3;
        }
        yiq_filtered.x += n * u_noise_strength * 0.4;
    }

    // ---- re-encode + clamp -------------------------------------------
    vec3 result = clamp(yiq_to_rgb(yiq_filtered), 0.0, 1.0);
    o_color = vec4(result, 0.0);
}
