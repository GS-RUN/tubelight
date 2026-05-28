// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// Pass 1 — Dithering reconstruction.
//
// Reads the dithering mask written by Pass 0 in the alpha channel.
// Where mask is high, replaces the texel with the average of its horizontal
// neighbours — that is what bandwidth-limited composite does to alternating
// columns in real CRT chains.
//
// This is the load-bearing pass for the Sonic Green Hill cascade demo:
// without it, even with a proper composite Pass −1 the rebuilt vertical
// bars stay visible as stripes; with it, they fuse into translucent water.

#version 450 core

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(binding = 1) uniform sampler2D u_source;       // Pass 0 output: rgb = source, a = dither mask

// Phase 3c: scalar/vec uniforms in explicit std140 cbuffer for
// deterministic HLSL layout. See pass4_bloom.frag for the rationale.
layout(std140, binding = 0) uniform PassUniforms {
    vec2  u_resolution;               // offset 0,  size 8
    float u_reconstruction_strength;  // offset 8,  size 4 — [0..1] global strength multiplier
    float _pad0;                      // offset 12, padding to 16
} u;
#define u_resolution                u.u_resolution
#define u_reconstruction_strength   u.u_reconstruction_strength

void main() {
    vec2 px = 1.0 / u_resolution;
    vec4 c  = texture(u_source, v_uv);
    vec3 cL = texture(u_source, v_uv - vec2(px.x, 0.0)).rgb;
    vec3 cR = texture(u_source, v_uv + vec2(px.x, 0.0)).rgb;
    vec3 cU = texture(u_source, v_uv - vec2(0.0, px.y)).rgb;
    vec3 cD = texture(u_source, v_uv + vec2(0.0, px.y)).rgb;

    float mask = c.a * u_reconstruction_strength;

    // Average with both neighbours: the original pixel + the alternating one.
    vec3 reconstructed = mix(
        (c.rgb + cL + cR) / 3.0,
        (c.rgb + cU + cD) / 3.0,
        0.5
    );

    vec3 result = mix(c.rgb, reconstructed, mask);

    // Strip the mask channel; subsequent passes don't need it.
    o_color = vec4(result, 1.0);
}
