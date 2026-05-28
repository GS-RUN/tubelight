// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// Pass 6 — Final composition.

#version 450 core

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

uniform sampler2D u_source;
uniform vec2  u_resolution;
uniform float u_barrel_strength;
uniform float u_vignette_strength;
uniform float u_gamma_display;
uniform float u_time;
uniform float u_warmup;

uniform int   u_monochrome;
uniform int   u_posterize_levels;
uniform vec3  u_phosphor_color;
uniform vec3  u_glass_tint;
uniform float u_glass_age;
uniform float u_target_aspect;
uniform int   u_bezel_style;     // 0=none 1=pvm 2=beige 3=wood 4=mac 5=generic
uniform int   u_has_bezel_image; // 0=use SDF, 1=sample u_bezel_tex
uniform sampler2D u_bezel_tex;   // optional photo-real bezel PNG

// ======================================================================
//  Bezels — programmatic but elaborate enough to read as actual CRT
//  housings. Each style has its own border thicknesses (left/top/right/
//  bottom, asymmetric for Mac Classic), base colour + grain, recessed
//  inner well, and one or two recognisable detail elements:
//
//    PVM:    small red power LED at lower-right.
//    Beige:  branding strip at the bottom with a subtle ridge.
//    Wood:   two channel knobs on the right side + speaker grille bars.
//    Mac:    asymmetric (thick bottom), rainbow Apple logo strip at top
//            and a horizontal "floppy slot" rectangle at the bottom.
//    Generic: dark grey with subtle gradient.
// ======================================================================

// Asymmetric borders per style (left, top, right, bottom in window-UV).
// Bumped substantially from the previous values so the frame is
// unambiguously visible as a monitor casing rather than a thin line.
vec4 bezel_borders(int style) {
    if (style == 1) return vec4(0.10, 0.10, 0.10, 0.15);  // PVM
    if (style == 2) return vec4(0.13, 0.13, 0.13, 0.18);  // beige terminal
    if (style == 3) return vec4(0.14, 0.12, 0.20, 0.18);  // wood (right=knobs)
    if (style == 4) return vec4(0.09, 0.11, 0.09, 0.28);  // Mac (thick bottom)
    if (style == 5) return vec4(0.08, 0.08, 0.08, 0.10);  // generic
    return vec4(0.0);
}

// Distance to a rounded-rect (positive outside, negative inside).
float sd_round_rect(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + vec2(r);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

// SDF disc.
float sd_circle(vec2 p, vec2 c, float r) {
    return length(p - c) - r;
}

vec3 bezel_color_full(vec2 win_uv, vec4 borders, int style) {
    // Picture rect in window-UV.
    vec2 picture_min = vec2(borders.x, borders.y);
    vec2 picture_max = vec2(1.0 - borders.z, 1.0 - borders.w);

    // Base colour + roughness per style.
    vec3 base       = vec3(0.05);
    vec3 highlight  = vec3(0.10);
    float roughness = 0.5;
    if (style == 1) { base = vec3(0.045, 0.045, 0.050); highlight = vec3(0.10, 0.10, 0.11); roughness = 0.30; }
    else if (style == 2) { base = vec3(0.72, 0.66, 0.55); highlight = vec3(0.85, 0.79, 0.67); roughness = 0.70; }
    else if (style == 3) { base = vec3(0.32, 0.20, 0.11); highlight = vec3(0.48, 0.32, 0.18); roughness = 0.95; }
    else if (style == 4) { base = vec3(0.83, 0.81, 0.74); highlight = vec3(0.94, 0.92, 0.85); roughness = 0.55; }
    else                 { base = vec3(0.10, 0.10, 0.11); highlight = vec3(0.20, 0.20, 0.22); roughness = 0.50; }

    // Top-down vertical shading for a faint sheen.
    vec3 col = mix(highlight, base, smoothstep(0.0, 1.0, win_uv.y));

    // Grain / wood / plastic texture.
    if (style == 3) {
        // Wood grain: horizontal stripes with low-frequency vertical wobble.
        float grain = sin(win_uv.y * 220.0 + win_uv.x * 4.0) * 0.6
                    + sin(win_uv.y * 80.0) * 0.4;
        col += grain * 0.022;
    } else {
        float grain = sin(win_uv.y * 800.0) * sin(win_uv.x * 13.0);
        col += grain * 0.012 * roughness;
    }

    // Outer-edge highlight: a bright rim around the very outside.
    float outer_d = min(min(win_uv.x, 1.0 - win_uv.x),
                        min(win_uv.y, 1.0 - win_uv.y));
    col += smoothstep(0.015, 0.0, outer_d) * highlight * 0.6;

    // Inner-edge: recessed well around the picture (the tube is set in
    // a depression). Soft, ~2 % deep.
    vec2 d = max(picture_min - win_uv, win_uv - picture_max);
    float dist = max(d.x, d.y);
    float ring = smoothstep(0.0, 0.022, dist) *
                 (1.0 - smoothstep(0.022, 0.045, dist));
    col *= 1.0 - 0.55 * ring;

    // ---- Style-specific details ------------------------------------

    if (style == 1) {
        // PVM: small red power LED at lower-right inside the bottom border.
        vec2 led_c = vec2(1.0 - borders.z * 0.4, 1.0 - borders.w * 0.45);
        float led_d = sd_circle(win_uv, led_c, 0.005);
        float glow  = smoothstep(0.014, 0.0, led_d);
        col = mix(col, vec3(1.0, 0.15, 0.10), smoothstep(0.005, 0.0, led_d));
        col += vec3(0.4, 0.05, 0.03) * glow * 0.5;
        // Subtle brand strip at the bottom-centre — slightly darker rect.
        vec2 brand_c = vec2(0.5, 1.0 - borders.w * 0.55);
        float brand  = sd_round_rect(win_uv - brand_c, vec2(0.10, 0.012), 0.006);
        col *= 1.0 - 0.18 * smoothstep(0.002, 0.0, brand);
    }
    else if (style == 2) {
        // Beige terminal: bottom branding strip with a ridge.
        float strip = smoothstep(picture_max.y + 0.012, picture_max.y + 0.018, win_uv.y) *
                      (1.0 - smoothstep(picture_max.y + 0.060, picture_max.y + 0.070, win_uv.y));
        col *= 1.0 - 0.10 * strip;
        // Small embossed dot suggesting a power button at lower-right.
        vec2 pb_c = vec2(1.0 - borders.z * 0.35, 1.0 - borders.w * 0.40);
        float pb_d = sd_circle(win_uv, pb_c, 0.010);
        col *= 1.0 - 0.25 * smoothstep(0.010, 0.0, pb_d);
        col += smoothstep(0.013, 0.010, pb_d) * vec3(0.06);  // rim highlight
    }
    else if (style == 3) {
        // Wood TV: two channel knobs on the right.
        vec2 k1 = vec2(1.0 - borders.z * 0.45, picture_min.y + 0.20);
        vec2 k2 = vec2(1.0 - borders.z * 0.45, picture_min.y + 0.45);
        float k1d = sd_circle(win_uv, k1, 0.035);
        float k2d = sd_circle(win_uv, k2, 0.030);
        col *= 1.0 - 0.35 * smoothstep(0.0, -0.005, k1d);
        col *= 1.0 - 0.35 * smoothstep(0.0, -0.005, k2d);
        // Knob outer ring highlight.
        col += vec3(0.20, 0.14, 0.08) * smoothstep(0.0, -0.002, k1d) *
                                         (1.0 - smoothstep(0.005, 0.015, abs(k1d)));
        // Speaker grille at the bottom: vertical bars.
        if (win_uv.y > picture_max.y + 0.020) {
            float bars = step(0.5, fract(win_uv.x * 60.0));
            col *= mix(1.0, 0.75, bars * 0.6);
        }
    }
    else if (style == 4) {
        // Mac Classic: rainbow Apple logo hint at the top + floppy slot at the bottom.
        // Rainbow strip (5 small horizontal bars at top).
        float top_strip = step(win_uv.y, borders.y * 0.55) *
                          step(0.41, win_uv.x) * step(win_uv.x, 0.59);
        if (top_strip > 0.5) {
            float k = (win_uv.x - 0.41) / 0.18;
            vec3 rainbow = vec3(0.0);
            if      (k < 0.16) rainbow = vec3(0.95, 0.30, 0.30);
            else if (k < 0.32) rainbow = vec3(0.95, 0.65, 0.20);
            else if (k < 0.48) rainbow = vec3(0.90, 0.90, 0.20);
            else if (k < 0.64) rainbow = vec3(0.30, 0.80, 0.30);
            else if (k < 0.80) rainbow = vec3(0.30, 0.55, 0.95);
            else               rainbow = vec3(0.65, 0.30, 0.85);
            col = mix(col, rainbow, 0.65);
        }
        // Floppy slot: horizontal dark slit, centred, in the bottom 60% of the
        // bottom border.
        vec2 slot_c = vec2(0.50, 1.0 - borders.w * 0.40);
        float slot_d = sd_round_rect(win_uv - slot_c, vec2(0.12, 0.008), 0.004);
        col = mix(col, vec3(0.06), smoothstep(0.002, 0.0, slot_d));
        // "Macintosh" badge area: a slightly darker rectangle below the screen.
        vec2 badge_c = vec2(0.50, picture_max.y + 0.025);
        float badge_d = sd_round_rect(win_uv - badge_c, vec2(0.06, 0.005), 0.002);
        col *= 1.0 - 0.12 * smoothstep(0.002, 0.0, badge_d);
    }
    else if (style == 5) {
        // Generic: just a subtle horizontal gradient + tiny LED.
        col *= mix(0.95, 1.05, smoothstep(0.0, 1.0, win_uv.x));
        vec2 led_c = vec2(1.0 - borders.z * 0.4, 1.0 - borders.w * 0.5);
        float led_d = sd_circle(win_uv, led_c, 0.004);
        col = mix(col, vec3(0.30, 0.85, 0.40), smoothstep(0.004, 0.0, led_d));
    }

    // Final darken on the very outside corners for "old, well-used" feel.
    float corner_dist = length(max(vec2(0.0), abs(win_uv - 0.5) - vec2(0.48)));
    col *= 1.0 - 0.20 * smoothstep(0.0, 0.025, corner_dist);

    return col;
}

vec2 barrel(vec2 uv, float k) {
    vec2 c = uv - 0.5;
    float r2 = dot(c, c);
    return 0.5 + c * (1.0 + k * r2);
}

vec2 magnetic_interference(vec2 uv, float t) {
    float dx = sin(t * 0.30 + uv.y * 6.0) * 0.0010;
    float dy = sin(t * 0.13 + uv.x * 8.0) * 0.0007;
    float hum = sin(t * 6.28 * 0.5 + uv.y * 80.0) * 0.0003;
    return uv + vec2(dx, dy + hum);
}

vec3 sample_with_convergence(vec2 uv) {
    vec2 from_center = uv - 0.5;
    float r2 = dot(from_center, from_center);
    vec2 dir = from_center * r2 * 0.004;
    float r = texture(u_source, uv + dir).r;
    float g = texture(u_source, uv).g;
    float b = texture(u_source, uv - dir).b;
    return vec3(r, g, b);
}

void main() {
    // Step 1: compute the bezel-trimmed "screen" rect (the area enclosed
    // by the frame). Inside this rect the CRT image lives; outside is
    // bezel (or, if style == 0, plain black bars / nothing).
    vec4  borders   = (u_bezel_style != 0) ? bezel_borders(u_bezel_style) : vec4(0.0);
    vec2  scrn_min  = vec2(borders.x, borders.y);
    vec2  scrn_max  = vec2(1.0 - borders.z, 1.0 - borders.w);

    // Outside the screen rect → bezel.
    if (v_uv.x < scrn_min.x || v_uv.x > scrn_max.x ||
        v_uv.y < scrn_min.y || v_uv.y > scrn_max.y) {
        // If the host has loaded a per-profile bezel PNG, sample it as
        // the photo-real bezel. The PNG's alpha tells us bezel vs.
        // screen — alpha ≥ 0.5 means "this pixel is part of the
        // monitor casing", so use the RGB straight. Alpha < 0.5 means
        // "transparent / screen cutout"; fall back to the SDF so we
        // still draw something there (typically the SDF dark frame).
        if (u_has_bezel_image == 1) {
            vec4 tex = texture(u_bezel_tex, v_uv);
            if (tex.a >= 0.5) {
                o_color = vec4(tex.rgb, 1.0);
                return;
            }
        }
        if (u_bezel_style != 0) {
            o_color = vec4(bezel_color_full(v_uv, borders, u_bezel_style), 1.0);
        } else {
            o_color = vec4(0.0, 0.0, 0.0, 1.0);
        }
        return;
    }

    // Step 2: remap UV into the screen rect → [0,1] for the CRT pipeline.
    vec2 screen_uv = (v_uv - scrn_min) / (scrn_max - scrn_min);

    // Step 3: apply target_aspect letterbox *inside* the screen rect.
    vec2 uv = screen_uv;
    if (u_target_aspect > 0.0) {
        // Effective aspect of the screen area (window aspect minus borders).
        float scrn_aspect = (u_resolution.x * (scrn_max.x - scrn_min.x)) /
                            max(u_resolution.y * (scrn_max.y - scrn_min.y), 1.0);
        if (scrn_aspect > u_target_aspect + 0.001) {
            float scale = u_target_aspect / scrn_aspect;
            float pad   = (1.0 - scale) * 0.5;
            if (uv.x < pad || uv.x > 1.0 - pad) {
                o_color = vec4(0.0, 0.0, 0.0, 1.0);
                return;
            }
            uv.x = (uv.x - pad) / scale;
        } else if (scrn_aspect < u_target_aspect - 0.001) {
            float scale = scrn_aspect / u_target_aspect;
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

    // Monochrome phosphor.
    if (u_monochrome == 1) {
        float Y_lin = max(dot(col, vec3(0.2126, 0.7152, 0.0722)), 0.0);
        if (u_posterize_levels > 1) {
            float Y_perc = pow(Y_lin, 1.0 / max(u_gamma_display, 1.0));
            float n = float(u_posterize_levels);
            Y_perc = floor(clamp(Y_perc, 0.0, 1.0) * n) / max(n - 1.0, 1.0);
            Y_lin = pow(Y_perc, u_gamma_display);
        }
        vec3 pcol = u_phosphor_color;
        if (max(pcol.r, max(pcol.g, pcol.b)) < 0.05) pcol = vec3(1.0);
        col = Y_lin * pcol;
    }

    // Glass tint + aging.
    vec3 tint = u_glass_tint;
    if (max(tint.r, max(tint.g, tint.b)) < 0.05) tint = vec3(1.0);
    if (u_glass_age > 0.0) {
        vec3 aged = vec3(1.05, 1.00, 0.85);
        tint = mix(tint, tint * aged, clamp(u_glass_age, 0.0, 1.0));
    }
    col *= tint;

    // Vignette.
    vec2 c = uv - 0.5;
    float dist = length(c) * 1.4142135;
    float vignette = 1.0 - u_vignette_strength * smoothstep(0.5, 1.0, dist);
    col *= vignette;

    // Warm-up.
    if (u_monochrome == 0) {
        float warmup = clamp(u_warmup, 0.0, 1.0);
        float brightness_curve = mix(0.55, 1.0, warmup);
        vec3 white_drift = mix(vec3(0.85, 0.95, 1.10), vec3(1.0), warmup);
        col = col * brightness_curve * white_drift;
    }

    // Soft highlight rolloff.
    col = max(col, 0.0);
    col = col / (1.0 + max(col - 0.92, 0.0));

    // Gamma encode.
    col = pow(col, vec3(1.0 / u_gamma_display));

    o_color = vec4(col, 1.0);
}
