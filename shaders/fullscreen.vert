// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// Default fullscreen-triangle vertex shader.
//
// Single-triangle technique: 3 vertices, no VBO needed beyond gl_VertexID.
// Produces a triangle that covers [-1, 1] x [-1, 1] with uv in [0, 1].
//
// Single source of truth — consumed both by:
//   - GL runtime via core/shader.cpp::default_fullscreen_vertex_source()
//   - DXIL build pipeline (glslang + SPIRV-Cross + dxc), Phase 3c.

#version 450 core

// Phase 3c: explicit layout location required by SPIR-V (glslang). GL
// is happy with explicit locations on `#version 450 core` so this is
// backward-compatible with the runtime GL path.
layout(location = 0) out vec2 v_uv;

// Vulkan SPIR-V exposes gl_VertexIndex; desktop OpenGL exposes
// gl_VertexID. CompileShaders.cmake defines TUBELIGHT_VULKAN for the
// glslang invocation; the GL runtime sees neither macro defined.
#ifdef TUBELIGHT_VULKAN
    #define TL_VERTEX_ID gl_VertexIndex
#else
    #define TL_VERTEX_ID gl_VertexID
#endif

void main() {
    // Generate (0,0), (2,0), (0,2) from vertex id 0,1,2
    v_uv = vec2((TL_VERTEX_ID << 1) & 2, TL_VERTEX_ID & 2);
    gl_Position = vec4(v_uv * 2.0 - 1.0, 0.0, 1.0);
}
