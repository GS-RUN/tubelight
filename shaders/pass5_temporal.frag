// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// Pass 5 — Temporal phosphor persistence (afterglow / "warm trail").
//
// Reads the previous frame's pass-5 output from u_prev_frame and blends it
// with the current frame using a per-channel decay factor. Modelled as a
// pure exponential afterglow: the phosphor takes the current excitation
// instantly (no inertia in receiving energy) but old emission lingers and
// fades over a few frames.
//
//   result = max(current, prev * persistence)
//
// The `max` combiner means new bright pixels overwrite the trail
// immediately (good for crisp edges) while old pixels keep glowing as
// they fade. Using `mix(current, prev, t)` instead would smear new
// content into a soft transition, which feels wrong for sharp pixel art.
//
// Per-channel persistence is what produces the colour CRT "warm trail":
// P22 red phosphor decays ~12.5× slower than green and blue, so bright
// moving objects leave a slight red ghost. For monochrome phosphors we
// use a uniform per-channel value.

#version 450 core

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

uniform sampler2D u_source;       // this frame's pass-4 output
uniform sampler2D u_prev_frame;   // last frame's pass-5 output (history FBO)

// Phase 3c: scalar/vec uniforms in explicit std140 cbuffer for
// deterministic HLSL layout. See pass4_bloom.frag for the rationale.
// std140 rule: a vec3 occupies 12 B + 4 B padding (16 B aligned). Place
// it first so the int after fills the last 4 B without padding waste.
layout(std140, binding = 0) uniform PassUniforms {
    vec3  u_persistence;    // offset 0,  size 12 — per-channel decay 0..1
    int   u_history_valid;  // offset 12, size 4  — 0 = first frame
} u;                        // total 16 B
#define u_persistence    u.u_persistence
#define u_history_valid  u.u_history_valid

void main() {
    vec3 current = texture(u_source, v_uv).rgb;

    // Skip the blend entirely on the first frame after a resize or when
    // persistence is effectively zero — otherwise we'd either sample
    // garbage or do a noop blend that costs a texture fetch.
    if (u_history_valid == 0 || dot(u_persistence, vec3(1.0)) < 1e-4) {
        o_color = vec4(current, 1.0);
        return;
    }

    vec3 prev     = texture(u_prev_frame, v_uv).rgb;
    vec3 trailing = prev * u_persistence;
    o_color       = vec4(max(current, trailing), 1.0);
}
