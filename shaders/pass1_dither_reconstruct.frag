// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// Pass 1 — Dithering reconstruction.
// F2: identity. F4 reads the dithering mask from Pass 0 and replaces
// pairs of alternating pixels with their average — emulating the natural
// blur that bandwidth-limited composite signal would produce on a CRT.
//
// This is the canonical "Sonic Green Hill cascade" fix: the waterfall
// uses 1-pixel-wide alternating columns that on RGB look like vertical
// bars but on composite NTSC fuse into transparent water.

#version 450 core

in vec2 v_uv;
out vec4 o_color;

uniform sampler2D u_source;

void main() {
    o_color = texture(u_source, v_uv);
}
