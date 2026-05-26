// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// Pass 0 — Analysis: dithering detection + luminance averaging.
// F2: identity (no analysis output consumed yet).
// F4 introduces: dithering pattern detection via 2x2 / 2x1 / 1x2 kernel
// comparing alternating pixels; mask is consumed by Pass 1.
// Pass 5 (F7) will consume the luminance average for voltage bloom.

#version 450 core

in vec2 v_uv;
out vec4 o_color;

uniform sampler2D u_source;

void main() {
    o_color = texture(u_source, v_uv);
}
