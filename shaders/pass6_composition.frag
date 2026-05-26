// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// Pass 6 — Final composition.
//
// Combines:
//   - Barrel distortion (CRT tube curvature)
//   - Slow magnetic / thermal interference (time-varying micro-distortion;
//     the "live" feeling that static shaders lack)
//   - Convergence offsets (cheap per-channel UV shift near corners)
//   - Vignette
//   - Display gamma encode (linear → sRGB)
//
// Input arrives in linear space (Pass 2 linearized). We encode back to
// display gamma right at the end.

#version 450 core

in vec2 v_uv;
out vec4 o_color;

uniform sampler2D u_source;
uniform vec2  u_resolution;
uniform float u_barrel_strength;     // typical 0.04..0.12 consumer
uniform float u_vignette_strength;   // [0..1]
uniform float u_gamma_display;       // ~2.2
uniform float u_time;                // seconds since attach (for magnetic + warmup)
uniform float u_warmup;              // 0=cold start, 1=fully warmed (180s curve)

vec2 barrel(vec2 uv, float k) {
    vec2 c = uv - 0.5;
    float r2 = dot(c, c);
    return 0.5 + c * (1.0 + k * r2);
}

vec2 magnetic_interference(vec2 uv, float t) {
    // Slow horizontal wave + slower vertical breathe; both micro-amplitude.
    float dx = sin(t * 0.30 + uv.y * 6.0) * 0.0003;
    float dy = sin(t * 0.13 + uv.x * 8.0) * 0.0002;
    return uv + vec2(dx, dy);
}

vec3 sample_with_convergence(vec2 uv) {
    // Convergence error: per-channel UV shift that grows toward corners.
    // Even high-end CRTs show ~0.5 pixel convergence at the edges.
    vec2 from_center = uv - 0.5;
    float r2 = dot(from_center, from_center);
    vec2 dir = from_center * r2 * 0.004;
    float r = texture(u_source, uv + dir).r;
    float g = texture(u_source, uv).g;
    float b = texture(u_source, uv - dir).b;
    return vec3(r, g, b);
}

void main() {
    vec2 uv = magnetic_interference(v_uv, u_time);
    uv = barrel(uv, u_barrel_strength);

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        o_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 col = sample_with_convergence(uv);

    // Vignette
    vec2 c = uv - 0.5;
    float dist = length(c) * 1.4142135;
    float vignette = 1.0 - u_vignette_strength * smoothstep(0.5, 1.0, dist);
    col *= vignette;

    // Warm-up: cold tubes start cooler in color temperature and dimmer.
    // u_warmup in [0..1] linearly multiplies brightness and shifts white
    // toward blue when cold.
    float warmup = clamp(u_warmup, 0.0, 1.0);
    float brightness_curve = mix(0.55, 1.0, warmup);
    vec3 white_drift = mix(vec3(0.85, 0.95, 1.10), vec3(1.0), warmup);
    col = col * brightness_curve * white_drift;

    // Gamma encode
    col = pow(max(col, 0.0), vec3(1.0 / u_gamma_display));

    o_color = vec4(col, 1.0);
}
