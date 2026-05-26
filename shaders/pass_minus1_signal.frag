// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// Pass -1 — Signal modeling (RF/Composite/SVideo/SCART/Component/VGA).
// F2: identity pass-through. F4 introduces:
//   - Bandwidth limit of luma and chroma channels
//   - Dot crawl (subcarrier interference)
//   - Ringing on high-contrast transitions
//   - Ghosting offset (RF)
//   - Line-coherent noise (RF / Composite)
//
// See specs/PLAN.LOCKED.md F4 and docs/research/SOURCES.md §4-§5.

#version 450 core

in vec2 v_uv;
out vec4 o_color;

uniform sampler2D u_source;

void main() {
    o_color = texture(u_source, v_uv);
}
