// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// Pass 3 — Phosphor mask (shadow / aperture-grille / slot / diamond / cgwg / dot-trio).
//
// Each mask layout is a 2D modulation over per-channel intensity. The mask
// is sampled at *display* pixel coordinates, not source pixel coordinates,
// to keep the physical pitch realistic (Constitution C2). 3D-relief shading
// (normal map + parallax + specular highlight) is added in F7.
//
// All math operates on the linearized luminance produced by Pass 2.

#version 450 core

in vec2 v_uv;
out vec4 o_color;

uniform sampler2D u_source;
uniform vec2  u_resolution;
uniform int   u_mask_type;        // 0=none 1=shadow 2=aperture 3=slot 4=diamond 5=cgwg 6=dot_trio
uniform float u_mask_strength;    // [0, 1]
uniform float u_mask_pitch_px;    // ~2..4 typical at 1080p

// ----- Mask functions -----------------------------------------------------

vec3 mask_aperture_grille(vec2 px, float pitch) {
    // Vertical R/G/B stripes; the Trinitron / Sony PVM signature.
    int slot = int(mod(px.x / pitch, 3.0));
    if (slot == 0) return vec3(1.0, 0.3, 0.3);
    if (slot == 1) return vec3(0.3, 1.0, 0.3);
    return                vec3(0.3, 0.3, 1.0);
}

vec3 mask_shadow(vec2 px, float pitch) {
    // Hexagonal triad: alternating rows offset by half a pitch.
    float row = floor(px.y / pitch);
    float xshift = (mod(row, 2.0) == 0.0) ? 0.0 : pitch * 0.5;
    int slot = int(mod((px.x + xshift) / pitch, 3.0));
    vec3 m = vec3(0.30);
    if      (slot == 0) m.r = 1.0;
    else if (slot == 1) m.g = 1.0;
    else                m.b = 1.0;
    return m;
}

vec3 mask_slot(vec2 px, float pitch) {
    // Slot mask: rectangular apertures, R/G/B per column, two rows per slot.
    int  col       = int(mod(px.x / pitch, 3.0));
    int  row_phase = int(mod(px.y / (pitch * 2.0), 2.0));
    float xshift   = (row_phase == 0) ? 0.0 : pitch * 1.5;
    int  slot_eff  = int(mod((px.x + xshift) / pitch, 3.0));
    vec3 m = vec3(0.30);
    if      (slot_eff == 0) m.r = 1.0;
    else if (slot_eff == 1) m.g = 1.0;
    else                    m.b = 1.0;
    return m;
}

vec3 mask_diamond(vec2 px, float pitch) {
    // Diamond / rhombic — Philips / NEC late CRTs.
    vec2 cell = px / pitch;
    float diamond = abs(fract(cell.x) - 0.5) + abs(fract(cell.y) - 0.5);
    int slot = int(mod(floor(cell.x) + floor(cell.y), 3.0));
    vec3 m = vec3(0.40);
    if      (slot == 0) m.r = 1.0;
    else if (slot == 1) m.g = 1.0;
    else                m.b = 1.0;
    return mix(m, vec3(0.20), smoothstep(0.35, 0.55, diamond));
}

vec3 mask_cgwg_mix(vec2 px, float pitch) {
    // cgwg-style hybrid: vertical RGB with sinusoidal horizontal modulation.
    int slot = int(mod(px.x / pitch, 3.0));
    vec3 m = vec3(0.5);
    if      (slot == 0) m = vec3(1.0, 0.5, 0.5);
    else if (slot == 1) m = vec3(0.5, 1.0, 0.5);
    else                m = vec3(0.5, 0.5, 1.0);
    float horiz = 0.85 + 0.15 * sin(px.y * 3.14159 / pitch);
    return m * horiz;
}

vec3 mask_dot_trio(vec2 px, float pitch) {
    // Dot-trio shadow mask: distinct circular phosphor dots in triads.
    vec2 cell = px / pitch;
    int idx = int(mod(floor(cell.x), 3.0));
    vec3 m = vec3(0.20);
    if      (idx == 0) m.r = 1.0;
    else if (idx == 1) m.g = 1.0;
    else               m.b = 1.0;
    float r = length(fract(cell) - 0.5);
    return mix(m, vec3(0.20), smoothstep(0.40, 0.50, r));
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

    // Blend between "no mask" (full passthrough) and "full mask" by strength.
    vec3 result = src * mix(vec3(1.0), mask, u_mask_strength);
    o_color = vec4(result, 1.0);
}
