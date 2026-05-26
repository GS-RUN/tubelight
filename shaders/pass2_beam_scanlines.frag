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

in vec2 v_uv;
out vec4 o_color;

uniform sampler2D u_source;
uniform vec2  u_resolution;
uniform float u_scanline_strength;   // [0, 1]
uniform float u_beam_width;          // ~1.0..2.0 typical
uniform float u_gamma_crt;           // ~2.5
uniform float u_scanline_count;      // visible scanlines per frame (240 NTSC, 288 PAL)

float relative_luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    vec3 src = texture(u_source, v_uv).rgb;

    // Linearize from display-gamma-encoded input to physical luminance.
    vec3 lin = pow(max(src, 0.0), vec3(u_gamma_crt));

    // We can't read the source-resolution from the input texture any more
    // (it's the output of the previous FBO pass, sized to the current
    // window). Use u_scanline_count explicitly — 240 NTSC raster visible
    // lines by default, configurable via menu.
    float scanline_y = v_uv.y * max(u_scanline_count, 1.0);
    float center_offset = abs(fract(scanline_y) - 0.5);  // [0, 0.5]

    // Beam width is brightness-dependent: bright pixels bloom wider.
    float lum  = relative_luminance(lin);
    float beam = mix(u_beam_width, u_beam_width * 1.8, smoothstep(0.0, 1.0, lum));

    // Gaussian profile (peak = 1.0 at scanline center).
    float intensity = exp(-pow(center_offset * 4.0 / max(beam, 1e-3), 2.0));

    // Combine: between scanlines we attenuate by (1 - strength); on the peak
    // we leave full luminance.
    float modulation = mix(1.0 - u_scanline_strength, 1.0, intensity);

    o_color = vec4(lin * modulation, 1.0);
}
