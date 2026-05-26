// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// Pass 5 — Temporal effects: per-channel phosphor persistence + voltage
// bloom + optional BFI (Black Frame Insertion).
//
// F2: identity. F7 introduces:
//   - Previous-frame texture sampling for exponential per-channel decay
//     (P22 red is the slowest, blue is the fastest; P31 monochrome has a
//     single slow decay).
//   - Frame-mean luminance read from Pass 0 used to widen the beam (voltage
//     bloom: the CRT power supply sags when whites fill the screen).
//   - BFI insertion every other frame for high-Hz displays.

#version 450 core

in vec2 v_uv;
out vec4 o_color;

uniform sampler2D u_source;

void main() {
    o_color = texture(u_source, v_uv);
}
