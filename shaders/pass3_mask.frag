// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// Pass 3 — Phosphor mask (shadow / aperture-grille / slot / diamond / cgwg / dot-trio).
//
// Each mask layout is a 2D modulation over per-channel intensity. The mask
// is sampled at *display* pixel coordinates, not source pixel coordinates,
// to keep the physical pitch realistic (Constitution C2).
//
// All math operates on the linearized luminance produced by Pass 2.
//
// Approach (rewritten 2026-05-26 sesión 3):
//   Each phosphor element is modelled as a Gaussian falloff around its
//   geometric centre. Three overlapping Gaussians per triad give natural,
//   anti-aliased phosphor stripes with realistic ~35-45% crosstalk between
//   adjacent channels — the same spillover real CRTs have due to electron
//   beam spread and phosphor grain. The previous `int(mod(px.x/pitch,3))`
//   slot picker produced HARD edges that aliased into visible interference
//   moiré at typical 2-4 px pitches, and the off-channel level of 0.3
//   produced unrealistic over-saturation.

#version 450 core

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

uniform sampler2D u_source;
uniform vec2  u_resolution;
uniform int   u_mask_type;        // 0=none 1=shadow 2=aperture 3=slot 4=diamond 5=cgwg 6=dot_trio
uniform float u_mask_strength;    // [0, 1]
uniform float u_mask_pitch_px;    // ~2..4 typical at 1080p

// ----- Gaussian helpers ---------------------------------------------------

float gaussian(float d, float sigma) {
    return exp(-d * d / max(2.0 * sigma * sigma, 1e-6));
}

// Stripe of phosphor at horizontal centre `cx` (in pixels) along axis u.
// Wrap around `period` so the stripe at one end of the cell also bleeds
// in from the other end (avoids a darker seam every triad).
float stripe(float u, float cx, float sigma, float period) {
    float a = gaussian(u - cx,            sigma);
    float b = gaussian(u - cx - period,   sigma);
    float c = gaussian(u - cx + period,   sigma);
    return a + b + c;
}

// 2D dot at (cx, cy) with independent sigmas.
float dot2d(vec2 p, vec2 c, vec2 sigma, vec2 period) {
    float result = 0.0;
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            vec2 d = (p - c) - vec2(period.x * float(dx), period.y * float(dy));
            result += exp(-(d.x * d.x) / max(2.0 * sigma.x * sigma.x, 1e-6)
                          -(d.y * d.y) / max(2.0 * sigma.y * sigma.y, 1e-6));
        }
    }
    return result;
}

// ----- Mask functions -----------------------------------------------------

// Aperture grille (Trinitron / Sony PVM signature): vertical R/G/B stripes.
// Gaussian profile per stripe → smooth gradients, no moiré aliasing.
vec3 mask_aperture_grille(vec2 px, float pitch) {
    float period = pitch * 3.0;
    float u      = mod(px.x, period);
    float sigma  = pitch * 0.70;            // ~38% crosstalk to neighbours
    float r = stripe(u, pitch * 0.5, sigma, period);
    float g = stripe(u, pitch * 1.5, sigma, period);
    float b = stripe(u, pitch * 2.5, sigma, period);
    return vec3(r, g, b);
}

// Shadow mask: hex-packed triad. Even rows R-G-B repeating, odd rows shifted
// by half a triad. Each phosphor is a circular Gaussian dot.
vec3 mask_shadow(vec2 px, float pitch) {
    float row_h    = pitch * 1.732;         // sqrt(3) — hex packing
    float row_idx  = floor(px.y / row_h);
    float row_y    = (row_idx + 0.5) * row_h;
    float xshift   = (mod(row_idx, 2.0) == 0.0) ? 0.0 : pitch * 1.5;
    float period_x = pitch * 3.0;
    float u        = mod(px.x - xshift, period_x);
    vec2  sigma    = vec2(pitch * 0.50, row_h * 0.50);
    float r = dot2d(vec2(u, px.y), vec2(pitch * 0.5, row_y), sigma, vec2(period_x, row_h * 2.0));
    float g = dot2d(vec2(u, px.y), vec2(pitch * 1.5, row_y), sigma, vec2(period_x, row_h * 2.0));
    float b = dot2d(vec2(u, px.y), vec2(pitch * 2.5, row_y), sigma, vec2(period_x, row_h * 2.0));
    return vec3(r, g, b);
}

// Slot mask: vertical R/G/B stripes broken into rectangular slots, with
// alternate rows offset by 1.5 pitch (typical 1990s consumer CRT).
vec3 mask_slot(vec2 px, float pitch) {
    float slot_h   = pitch * 2.0;
    float row_idx  = floor(px.y / slot_h);
    float xshift   = (mod(row_idx, 2.0) == 0.0) ? 0.0 : pitch * 1.5;
    float period   = pitch * 3.0;
    float u        = mod(px.x - xshift, period);
    float sigma_x  = pitch * 0.70;
    // Vertical slot modulation: bright in the middle of each slot, slightly
    // dimmer at the seams between rows.
    float seam_y   = mod(px.y, slot_h) / slot_h;       // [0, 1)
    float vmod     = 0.85 + 0.15 * sin(seam_y * 3.14159);
    float r = stripe(u, pitch * 0.5, sigma_x, period);
    float g = stripe(u, pitch * 1.5, sigma_x, period);
    float b = stripe(u, pitch * 2.5, sigma_x, period);
    return vec3(r, g, b) * vmod;
}

// Diamond / rhombic phosphor (late Philips / NEC). Triads laid out on a
// diamond lattice; each phosphor is a Gaussian dot offset every other row.
vec3 mask_diamond(vec2 px, float pitch) {
    float period_x = pitch * 3.0;
    float row_h    = pitch * 1.5;
    float row_idx  = floor(px.y / row_h);
    float xshift   = (mod(row_idx, 2.0) == 0.0) ? 0.0 : pitch * 1.5;
    float row_y    = (row_idx + 0.5) * row_h;
    float u        = mod(px.x - xshift, period_x);
    vec2  sigma    = vec2(pitch * 0.45, row_h * 0.45);
    float r = dot2d(vec2(u, px.y), vec2(pitch * 0.5, row_y), sigma, vec2(period_x, row_h * 2.0));
    float g = dot2d(vec2(u, px.y), vec2(pitch * 1.5, row_y), sigma, vec2(period_x, row_h * 2.0));
    float b = dot2d(vec2(u, px.y), vec2(pitch * 2.5, row_y), sigma, vec2(period_x, row_h * 2.0));
    return vec3(r, g, b);
}

// cgwg-style hybrid: aperture grille with a soft horizontal beam modulation,
// emulating the inter-scanline darkening of the original cgwg shaders.
vec3 mask_cgwg_mix(vec2 px, float pitch) {
    vec3 ag   = mask_aperture_grille(px, pitch);
    float mod_h = 0.85 + 0.15 * sin(px.y * 3.14159 / pitch);
    return ag * mod_h;
}

// Dot-trio shadow mask: discrete circular phosphor dots in 3-wide triads,
// one dot per channel per cell. No hex offset (every row aligned).
vec3 mask_dot_trio(vec2 px, float pitch) {
    float period_x = pitch * 3.0;
    float period_y = pitch * 2.0;
    float row_idx  = floor(px.y / period_y);
    float row_y    = (row_idx + 0.5) * period_y;
    float u        = mod(px.x, period_x);
    vec2  sigma    = vec2(pitch * 0.45, pitch * 0.85);
    float r = dot2d(vec2(u, px.y), vec2(pitch * 0.5, row_y), sigma, vec2(period_x, period_y));
    float g = dot2d(vec2(u, px.y), vec2(pitch * 1.5, row_y), sigma, vec2(period_x, period_y));
    float b = dot2d(vec2(u, px.y), vec2(pitch * 2.5, row_y), sigma, vec2(period_x, period_y));
    return vec3(r, g, b);
}

// ----- Main ---------------------------------------------------------------

void main() {
    vec3 src = texture(u_source, v_uv).rgb;
    vec2 px  = v_uv * u_resolution;

    vec3 mask;
    if      (u_mask_type == 1) mask = mask_shadow         (px, u_mask_pitch_px);
    else if (u_mask_type == 2) mask = mask_aperture_grille(px, u_mask_pitch_px);
    else if (u_mask_type == 3) mask = mask_slot           (px, u_mask_pitch_px);
    else if (u_mask_type == 4) mask = mask_diamond        (px, u_mask_pitch_px);
    else if (u_mask_type == 5) mask = mask_cgwg_mix       (px, u_mask_pitch_px);
    else if (u_mask_type == 6) mask = mask_dot_trio       (px, u_mask_pitch_px);
    else                       mask = vec3(1.0);

    // Energy normalisation: with Gaussian phosphors at sigma~0.7*pitch the
    // sum of three overlapping channels is ~constant ≈ 1.6 across a triad,
    // so each channel averages to ~0.55. Divide by that average so the
    // mask at full strength only dims the picture by ~25% (realistic light
    // loss of an aperture grille — shadow masks lose more, ~50%). Without
    // this, mask_strength=1 dimmed by ~45% and the user had to crank
    // global intensity to compensate.
    float ch_avg = 0.55;
    mask /= ch_avg;
    // Soft clip to avoid peaks > ~1.8 amplifying highlights through Pass 4.
    mask = min(mask, vec3(1.8));

    // Blend between "no mask" (full passthrough) and "full mask" by strength.
    vec3 result = src * mix(vec3(1.0), mask, u_mask_strength);
    o_color = vec4(result, 1.0);
}
