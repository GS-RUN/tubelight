// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// Pass 2 — Electron beam spreading + scanlines + CRT-gamma linearization.
//
// Modeled phenomena (specs/DESIGN.md §D1 + docs/research/SOURCES.md §4):
//   - CRT response gamma ~2.5 (not the 2.2 of sRGB) — linearizes input
//     so subsequent passes operate in physically-meaningful luminance space.
//   - Vertical Gaussian beam profile per scanline. Brighter beams spread
//     wider (basic blooming proxy — the full voltage bloom is in Pass 5).
//   - Scanline strength modulates inter-scanline regions.
//
// All output stays in linear space (display gamma is re-applied in Pass 6).

#version 450 core

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

uniform sampler2D u_source;
uniform vec2  u_resolution;
uniform float u_scanline_strength;   // [0, 1]
uniform float u_beam_width;          // ~1.0..2.0 typical
uniform float u_gamma_crt;           // ~2.5
uniform float u_scanline_count;      // visible scanlines per frame (240 NTSC, 288 PAL)
uniform float u_frame_mean_lum;      // 0..1 average luminance of current frame

float relative_luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Faux analog antialiasing for the source-pixel grid. A real CRT
// has a Gaussian beam spot that integrates over fractional pixel
// positions; we approximate by sampling four sub-pixel offsets and
// averaging. Cheap (4 taps), kills the worst staircase on diagonals.
vec3 sample_aa(vec2 uv) {
    vec2 px = 0.5 / u_resolution;
    vec3 a = texture(u_source, uv + vec2(-px.x, -px.y)).rgb;
    vec3 b = texture(u_source, uv + vec2( px.x, -px.y)).rgb;
    vec3 c = texture(u_source, uv + vec2(-px.x,  px.y)).rgb;
    vec3 d = texture(u_source, uv + vec2( px.x,  px.y)).rgb;
    return (a + b + c + d) * 0.25;
}

void main() {
    // Sub-pixel AA instead of a single texel fetch.
    vec3 src = sample_aa(v_uv);

    // Linearize from display-gamma-encoded input to physical luminance.
    vec3 lin = pow(max(src, 0.0), vec3(u_gamma_crt));

    // Scanline period: u_scanline_count visible lines per frame
    // (240 NTSC default, configurable).
    float scanline_y = v_uv.y * max(u_scanline_count, 1.0);
    float center_offset = abs(fract(scanline_y) - 0.5);  // [0, 0.5]

    // Beam width modulation has TWO factors:
    //
    //   (a) Brightness-dependent: bright pixels bloom wider. Used to
    //       be a 1.0→1.8× ramp; now 1.0→2.5× with a smoother knee so
    //       very bright pixels visibly "fatten" their scanline (the
    //       intensity-dependent beam bloom).
    //
    //   (b) Voltage bloom: when the whole frame is bright the CRT
    //       power supply sags and the beam current goes up, widening
    //       every beam slightly. u_frame_mean_lum drives a small
    //       global +30 % widening at full white.
    float lum   = relative_luminance(lin);
    float beam  = mix(u_beam_width, u_beam_width * 2.5, smoothstep(0.0, 1.0, lum));
    beam       *= 1.0 + 0.30 * clamp(u_frame_mean_lum, 0.0, 1.0);

    // Gaussian profile (peak = 1.0 at scanline center).
    float intensity = exp(-pow(center_offset * 4.0 / max(beam, 1e-3), 2.0));

    // Combine: between scanlines we attenuate by (1 - strength); on the peak
    // we leave full luminance.
    float modulation = mix(1.0 - u_scanline_strength, 1.0, intensity);

    o_color = vec4(lin * modulation, 1.0);
}
