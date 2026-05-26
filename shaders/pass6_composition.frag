// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// Pass 6 — Final composition: barrel distortion + vignette + display gamma.
//
// F2 (current): barrel + vignette + gamma encode. The "input is linear"
// invariant established by Pass 2 makes the gamma encode here unambiguous.
//
// F7 adds:
//   - Convergence offsets (per-channel UV shifts in corners)
//   - Slow magnetic / thermal interference (time-varying small distortion)
//   - Ambient glass reflection overlay

#version 450 core

in vec2 v_uv;
out vec4 o_color;

uniform sampler2D u_source;
uniform vec2  u_resolution;
uniform float u_barrel_strength;    // typical 0.04..0.12 for consumer CRTs
uniform float u_vignette_strength;  // [0, 1]
uniform float u_gamma_display;      // ~2.2 for sRGB monitors

vec2 barrel(vec2 uv, float k) {
    vec2 c = uv - 0.5;
    float r2 = dot(c, c);
    return 0.5 + c * (1.0 + k * r2);
}

void main() {
    vec2 uv = barrel(v_uv, u_barrel_strength);

    // Out-of-bounds after barrel = pure black (curved tube edge dropout).
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        o_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 col = texture(u_source, uv).rgb;

    // Vignette: distance from center, normalized to [0, ~1.41].
    vec2 c = uv - 0.5;
    float dist = length(c) * 1.4142135;
    float vignette = 1.0 - u_vignette_strength * smoothstep(0.5, 1.0, dist);
    col *= vignette;

    // Gamma encode for the display. Input has been linear since Pass 2.
    col = pow(max(col, 0.0), vec3(1.0 / u_gamma_display));

    o_color = vec4(col, 1.0);
}
