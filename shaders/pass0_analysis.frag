// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// Pass 0 — Analysis: dithering detection + luminance read.
//
// We look at the current texel and its immediate neighbors to see if the
// local pattern matches a 2-pixel-wide dithering convention common in
// 8/16-bit consoles (alternating columns or rows):
//
//   . X . X . X     <- vertical stripes (Sonic Green Hill cascada)
//
// The detected mask is written to the alpha channel of the output texel
// (1.0 = strongly dithered, 0.0 = not). Pass 1 reads alpha and chooses
// between identity and average reconstruction.
//
// RGB is passed through unchanged so Pass 1 sees the same source.

#version 450 core

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

uniform sampler2D u_source;

// Phase 3c: scalar/vec uniforms in explicit std140 cbuffer for
// deterministic HLSL layout. See pass4_bloom.frag for the rationale.
layout(std140, binding = 0) uniform PassUniforms {
    vec2  u_resolution;                // offset 0,  size 8
    float u_dither_detect_threshold;   // offset 8,  size 4
    float _pad0;                       // offset 12, padding to 16
} u;
#define u_resolution                u.u_resolution
#define u_dither_detect_threshold   u.u_dither_detect_threshold

float relative_luminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    vec2 px = 1.0 / u_resolution;
    vec3 c   = texture(u_source, v_uv).rgb;
    vec3 cL  = texture(u_source, v_uv - vec2(px.x, 0.0)).rgb;
    vec3 cR  = texture(u_source, v_uv + vec2(px.x, 0.0)).rgb;
    vec3 cU  = texture(u_source, v_uv - vec2(0.0, px.y)).rgb;
    vec3 cD  = texture(u_source, v_uv + vec2(0.0, px.y)).rgb;
    vec3 cLL = texture(u_source, v_uv - vec2(2.0 * px.x, 0.0)).rgb;
    vec3 cRR = texture(u_source, v_uv + vec2(2.0 * px.x, 0.0)).rgb;

    // Vertical stripe pattern (most common on Sega): same color as LL and RR,
    // different from L and R.
    float horiz_alt = step(u_dither_detect_threshold, distance(c,  cL))
                    * step(u_dither_detect_threshold, distance(c,  cR))
                    * (1.0 - step(u_dither_detect_threshold, distance(c, cLL)))
                    * (1.0 - step(u_dither_detect_threshold, distance(c, cRR)));

    // Horizontal stripe pattern (less common; some PS1 fog passes).
    vec3 cUU = texture(u_source, v_uv - vec2(0.0, 2.0 * px.y)).rgb;
    vec3 cDD = texture(u_source, v_uv + vec2(0.0, 2.0 * px.y)).rgb;
    float vert_alt = step(u_dither_detect_threshold, distance(c, cU))
                   * step(u_dither_detect_threshold, distance(c, cD))
                   * (1.0 - step(u_dither_detect_threshold, distance(c, cUU)))
                   * (1.0 - step(u_dither_detect_threshold, distance(c, cDD)));

    float mask = max(horiz_alt, vert_alt);

    o_color = vec4(c, mask);
}
