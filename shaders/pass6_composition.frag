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

// Phosphor / glass tint (from CRTProfile via Pipeline::apply_crt_profile).
uniform int   u_monochrome;          // 0 = color CRT, 1 = single-phosphor (P31/P3/P4...)
uniform int   u_posterize_levels;    // 0 = no quantisation; 2 = pure 1-bit (Mac);
                                     //                     4..8 = text-terminal feel
uniform vec3  u_phosphor_color;      // colour of the monochrome phosphor
uniform vec3  u_glass_tint;          // per-channel multiplier for glass colour
uniform float u_glass_age;           // [0..1] adds amber drift on top of tint
uniform float u_target_aspect;       // 4/3, 5/4, 16/9... 0 = fill window (no bars)

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
    // Letterbox / pillarbox: if the window aspect differs from the CRT's
    // native aspect, black bars are inserted so the picture isn't stretched.
    vec2 uv = v_uv;
    if (u_target_aspect > 0.0) {
        float win_aspect = u_resolution.x / max(u_resolution.y, 1.0);
        if (win_aspect > u_target_aspect + 0.001) {
            // Window wider than target → vertical pillarbox bars.
            float scale = u_target_aspect / win_aspect;
            float pad   = (1.0 - scale) * 0.5;
            if (uv.x < pad || uv.x > 1.0 - pad) {
                o_color = vec4(0.0, 0.0, 0.0, 1.0);
                return;
            }
            uv.x = (uv.x - pad) / scale;
        } else if (win_aspect < u_target_aspect - 0.001) {
            // Window taller than target → horizontal letterbox bars.
            float scale = win_aspect / u_target_aspect;
            float pad   = (1.0 - scale) * 0.5;
            if (uv.y < pad || uv.y > 1.0 - pad) {
                o_color = vec4(0.0, 0.0, 0.0, 1.0);
                return;
            }
            uv.y = (uv.y - pad) / scale;
        }
    }

    uv = magnetic_interference(uv, u_time);
    uv = barrel(uv, u_barrel_strength);

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        o_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 col = sample_with_convergence(uv);

    // Monochrome phosphor: collapse to luminance, optionally posterize to
    // emulate the limited tonal range of text terminals (or true 1-bit Mac
    // Classic / Lisa), then re-color through the phosphor's chromaticity.
    //
    // col arrives in CRT-linear space (Pass 2 applied pow(u_gamma_crt)).
    // We compute Y in linear, multiply by the phosphor's emission
    // scaling (treated as a linear multiplier — P31 green is `(0.1, 1.3,
    // 0.2)` meaning the green channel emits ~13x more than blue), and
    // let the final display-gamma encode at the bottom of this shader
    // turn it back into sRGB. Posterize is applied in perceptual space
    // so the discrete steps match the eye, not the linear scale.
    if (u_monochrome == 1) {
        float Y_lin = max(dot(col, vec3(0.2126, 0.7152, 0.0722)), 0.0);
        if (u_posterize_levels > 1) {
            // Quantise in sRGB space so the bands are visually even.
            float Y_perc = pow(Y_lin, 1.0 / max(u_gamma_display, 1.0));
            float n = float(u_posterize_levels);
            Y_perc = floor(clamp(Y_perc, 0.0, 1.0) * n) / max(n - 1.0, 1.0);
            Y_lin = pow(Y_perc, u_gamma_display);
        }
        vec3 pcol = u_phosphor_color;
        if (max(pcol.r, max(pcol.g, pcol.b)) < 0.05) pcol = vec3(1.0);
        col = Y_lin * pcol;
    }

    // Glass tint + aging amber drift. Pure 1.0 tint with age 0.0 is no-op.
    // Safety: if the uniform somehow arrives as (0,0,0) (e.g. driver optimised
    // it out), fall back to white so we never multiply the picture by zero.
    vec3 tint = u_glass_tint;
    if (max(tint.r, max(tint.g, tint.b)) < 0.05) tint = vec3(1.0);
    if (u_glass_age > 0.0) {
        vec3 aged = vec3(1.05, 1.00, 0.85);
        tint = mix(tint, tint * aged, clamp(u_glass_age, 0.0, 1.0));
    }
    col *= tint;

    // Vignette
    vec2 c = uv - 0.5;
    float dist = length(c) * 1.4142135;
    float vignette = 1.0 - u_vignette_strength * smoothstep(0.5, 1.0, dist);
    col *= vignette;

    // Warm-up: cold tubes start dimmer and cooler. For colour CRTs we model
    // 0.55..1.0 brightness over 180s and a cool→neutral white drift. For
    // monochrome phosphors (terminals, vintage B&W TVs) the user expects the
    // signature saturated colour from the first frame, so we skip the cold
    // warmup curve entirely.
    if (u_monochrome == 0) {
        float warmup = clamp(u_warmup, 0.0, 1.0);
        float brightness_curve = mix(0.55, 1.0, warmup);
        vec3 white_drift = mix(vec3(0.85, 0.95, 1.10), vec3(1.0), warmup);
        col = col * brightness_curve * white_drift;
    }

    // Soft highlight rolloff (pre-gamma tonemap). Pass 4 adds bloom +
    // halation additively on top of the source, which can push values
    // above 1.0 in bright regions. Knee starts at 0.92 (only kicks in
    // for the genuinely bright pixels that would otherwise clip) and
    // asymptotically maps anything brighter toward 1.0 — preserves
    // highlight detail without compressing the normal range.
    col = max(col, 0.0);
    col = col / (1.0 + max(col - 0.92, 0.0));

    // Gamma encode
    col = pow(col, vec3(1.0 / u_gamma_display));

    o_color = vec4(col, 1.0);
}
