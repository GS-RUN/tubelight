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

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(binding = 1) uniform sampler2D u_source;  // Phase 3c convention: t1

// Phase 3c: scalar/vec uniforms wrapped in explicit std140 block. Produces
// a deterministic HLSL cbuffer on the SPIRV-Cross → dxc path (no $Globals
// reorder roulette), and a matching C++ POD in src/render/pass_uniforms.h.
// Layout is std140: each vec2 takes 8 B aligned to 8 B; each float takes
// 4 B with no special alignment; the block is rounded up to a multiple of
// 16 B at the end. We arrange fields tightly: vec2 first (8 B), then the
// 2 floats fill the remaining 8 B of the same 16-byte slot.
layout(std140, binding = 0) uniform PassUniforms {
    vec2  u_resolution;            // offset 0,  size 8
    float u_bloom_strength;        // offset 8,  size 4
    float u_halation_strength;     // offset 12, size 4
} u;                               // total 16 B (cbuffer-perfect)
#define u_resolution         u.u_resolution
#define u_bloom_strength     u.u_bloom_strength
#define u_halation_strength  u.u_halation_strength

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
