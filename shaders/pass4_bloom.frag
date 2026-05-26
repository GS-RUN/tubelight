// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// Pass 4 — Bloom (luminance) + halation (chromatic per-channel blur).
//
// F2: single-pass approximate separable kernel (9 taps horizontal, no vertical).
// F7: replaced by proper two-stage separable Gaussian with intermediate FBO.
//
// Halation: in a real CRT the electron bounce inside the tube spreads red
// further than green, and green further than blue. We model this with
// different blur radii per channel — the result is the orange halo around
// bright objects.

#version 450 core

in vec2 v_uv;
out vec4 o_color;

uniform sampler2D u_source;
uniform vec2  u_resolution;
uniform float u_bloom_strength;
uniform float u_halation_strength;

// Gaussian weights for 9 taps (truncated normal, sigma ~ 1.5).
const float kWeights[5] = float[5](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

vec3 blur_h_with_radius(vec2 uv, vec2 px, float radius_scale) {
    vec3 sum = texture(u_source, uv).rgb * kWeights[0];
    float wsum = kWeights[0];
    for (int i = 1; i < 5; ++i) {
        vec2 off = vec2(px.x * float(i) * radius_scale, 0.0);
        sum += texture(u_source, uv + off).rgb * kWeights[i];
        sum += texture(u_source, uv - off).rgb * kWeights[i];
        wsum += 2.0 * kWeights[i];
    }
    return sum / max(wsum, 1e-4);
}

void main() {
    vec3 src = texture(u_source, v_uv).rgb;
    vec2 px  = 1.0 / u_resolution;

    // Standard luminance bloom (single radius, all channels equal weight).
    vec3 bloom = blur_h_with_radius(v_uv, px, 2.0);

    // Halation: per-channel radii (R = widest).
    vec3 halR  = blur_h_with_radius(v_uv, px, 4.0);
    vec3 halG  = blur_h_with_radius(v_uv, px, 2.5);
    vec3 halB  = blur_h_with_radius(v_uv, px, 1.5);
    vec3 halation = vec3(halR.r, halG.g, halB.b);

    vec3 result = src
                + bloom    * u_bloom_strength
                + halation * u_halation_strength * 0.5;

    o_color = vec4(result, 1.0);
}
