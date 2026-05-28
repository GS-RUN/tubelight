// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)
//
// Phase 3c of ADR-0002 — opaque resource handles for IRenderBackend v2.
//
// Pipeline holds these instead of GL-specific FBO/Texture2D/ShaderProgram,
// so the same orchestration code drives GL or D3D12 (Phase 3c). The
// backend maintains its own pool indexed by handle.id; id == 0 is the
// reserved "invalid" sentinel.
//
// Why opaque-by-id and not interfaces / variants / templates: see
// specs/phase-3c/DESIGN.md §D3c-1. Same pattern as bgfx / The Forge.

#pragma once

#include <cstddef>
#include <cstdint>

namespace tubelight {

struct TextureHandle {
    uint32_t id = 0;
    bool is_valid() const { return id != 0; }
};

struct RenderTargetHandle {
    uint32_t id = 0;
    bool is_valid() const { return id != 0; }
};

struct PassHandle {
    uint32_t id = 0;
    bool is_valid() const { return id != 0; }
};

// Pixel format enumeration. Only the two we actually use in Phase 3c are
// listed — adding HDR / depth / etc. is mechanical when we get there.
enum class PixelFormat {
    RGBA8_UNORM,   // 8-bit sRGB-equivalent — swap chain + uploaded source
    RGBA16_FLOAT,  // 16-bit half — pipeline intermediate FBOs (matches GL_RGBA16F)
};

struct TextureDesc {
    int width  = 0;
    int height = 0;
    PixelFormat format = PixelFormat::RGBA8_UNORM;
    // Mip levels, sampler params, etc. deferred. Tubelight uses one mip,
    // linear-clamp sampler for every texture in the pipeline.
};

struct PassDesc {
    // 0..7 maps to Pass -1..Pass 6 in the order kPassFilenames[] declares
    // (see core/pipeline.h). Drives which DXIL gets loaded by D3D12 and
    // which GLSL gets compiled by GL.
    int    pass_index = -1;
    // sizeof(PassUniforms_N). See render/pass_uniforms.h. Backend uses
    // this for the constant buffer size on D3D12 and for assert-debugging
    // set_uniform_block() calls on every backend.
    size_t uniform_block_bytes = 0;
    // 1..3. Slot 0 is always u_source. Slot 1 (pass 5) is u_prev_frame.
    // Slot 2 (pass 6) is u_bezel_tex. Fixed by convention so the D3D12
    // root signature can be static.
    int    texture_slot_count = 1;
};

} // namespace tubelight
