# Contracts — Phase 3c (Tubelight)

## API pública (C++ headers expuestos por `src/render/backend.h`)

### C3c-API1 — Handles opacos

```cpp
namespace tubelight {

struct TextureHandle      { uint32_t id = 0; bool is_valid() const { return id != 0; } };
struct RenderTargetHandle { uint32_t id = 0; bool is_valid() const { return id != 0; } };
struct PassHandle         { uint32_t id = 0; bool is_valid() const { return id != 0; } };

enum class PixelFormat {
    RGBA8_UNORM,        // 8-bit, swap chain backbuffer + uploaded source images
    RGBA16_FLOAT,       // 16-bit half-float, pipeline intermediate FBOs
};

struct TextureDesc {
    int width  = 0;
    int height = 0;
    PixelFormat format = PixelFormat::RGBA8_UNORM;
    // Mip levels, sampler params, etc. deferred to later phases.
};

struct PassDesc {
    // Path to compiled bytecode relative to the binary's shader dir.
    // GL backend ignores these and falls back to load_<pass_index>.frag.
    // D3D12 backend loads dxil/pass_<index>.dxil + dxil/fullscreen.dxil.
    int pass_index = -1;        // 0..7 maps to Pass -1..6 in pipeline.h order
    size_t uniform_block_bytes; // sizeof(PassUniforms_N)
    int    texture_slot_count;  // 1..3 (source, prev_frame, bezel)
};

} // namespace tubelight
```

### C3c-API2 — `IRenderBackend` extension

```cpp
class IRenderBackend {
public:
    // Phase 3a/3b methods (unchanged): init/shutdown/resize/begin_frame/
    // bind_default_framebuffer/set_viewport/clear_color/
    // draw_fullscreen_quad/end_frame/supports_pipeline/name

    // Phase 3c additions ----------------------------------------------
    virtual TextureHandle      create_texture(const TextureDesc&)                = 0;
    virtual RenderTargetHandle create_render_target(int w, int h, PixelFormat)   = 0;
    virtual PassHandle         create_pass(const PassDesc&)                      = 0;

    virtual void destroy_texture(TextureHandle)                                  = 0;
    virtual void destroy_render_target(RenderTargetHandle)                       = 0;
    virtual void destroy_pass(PassHandle)                                        = 0;

    virtual bool upload_texture_rgba8(TextureHandle, const void* data,
                                      int width, int height)                     = 0;
    virtual void copy_rt_to_texture(RenderTargetHandle src, TextureHandle dst)   = 0;

    virtual void bind_render_target(RenderTargetHandle)                          = 0;
    virtual void bind_pass(PassHandle)                                           = 0;
    virtual void bind_texture(int slot, TextureHandle)                           = 0;
    virtual void set_uniform_block(PassHandle, const void* data, size_t bytes)   = 0;
};
```

**Invariantes**:
- I1: handles devueltos por `create_*` son válidos hasta su `destroy_*`
  o hasta `shutdown()`. Reuse de IDs liberados es opcional (no se
  garantiza).
- I2: `bind_render_target({0})` (handle inválido) equivale a la
  default framebuffer (swap chain backbuffer). GLBackend mapea a
  framebuffer 0; D3D12Backend mapea al RTV del backbuffer actual.
- I3: `set_uniform_block` debe llamarse DESPUÉS de `bind_pass` para esa
  pasada. El binding del CB ocurre en el bind_pass; los datos se
  actualizan in-place hasta el siguiente bind_pass + set_uniform_block.
- I4: `bytes` en `set_uniform_block` debe coincidir EXACTAMENTE con
  `PassDesc::uniform_block_bytes` declarado en `create_pass`. Mismatch
  → assert en debug, undefined behavior en release.
- I5: el slot 0 en `bind_texture` es siempre `u_source` (input
  principal); slot 1 es `u_prev_frame` (pass 5); slot 2 es `u_bezel_tex`
  (pass 6). Convención fija para que el root signature D3D12 sea
  estático.

### C3c-API3 — `Pipeline` lifecycle (cambio retro-compatible)

`Pipeline::create()` ya existe. Internamente cambia su construcción de
recursos:

```cpp
// ANTES (v0.2.0-alpha.0):
for (int i = 0; i < kPassCount; ++i) {
    fbos_[i].create(w, h, GL_RGBA16F);   // GL-direct
    shaders_[i].build_from_files(...);   // GL-direct
}

// DESPUÉS (Phase 3c):
for (int i = 0; i < kPassCount; ++i) {
    rt_handles_[i] = backend_->create_render_target(w, h, PixelFormat::RGBA16_FLOAT);
    pass_handles_[i] = backend_->create_pass({i, sizeof(PassUniforms[i]), slots[i]});
}
history_rt_  = backend_->create_render_target(w, h, PixelFormat::RGBA16_FLOAT);
history_tex_ = backend_->create_texture({w, h, PixelFormat::RGBA16_FLOAT});
```

La firma pública de `Pipeline::create(int w, int h)` NO cambia. La
firma pública de `Pipeline::render_to_screen(uint32_t source_tex)`
cambia a `render_to_screen(TextureHandle source)`. Es breaking change
para llamadores externos; en este repo los llamadores son
`run_shader_only` (main.cpp) y `overlay_mode_win.cpp`, ambos en árbol.

### C3c-API4 — `--renderer` CLI (extensión retro-compatible)

Sin cambios sintácticos. Semánticamente:
- `--renderer gl` (default): comportamiento idéntico a v0.1.7. GL-only,
  todas las features.
- `--renderer dx12`: en `--shader-only` produce el output CRT
  completo (lo que antes era el clear azul de 3b). En modo overlay
  emite warning y fallback a gl (C3c-4 lo prohíbe en 3c).

## Formatos de datos

### C3c-F1 — Layout de `PassUniforms_N` POD

Header nuevo `src/render/pass_uniforms.h`:

```cpp
namespace tubelight {

// Alineado a 16 bytes (D3D12 constant buffer requirement).
// HLSL ve estos structs como cbuffer { ... } : register(b0).

#pragma pack(push, 16)

struct PassUniforms_PassMinus1 {
    float u_luma_mhz;
    float u_chroma_i_mhz;
    float u_chroma_q_mhz;
    float u_dot_crawl_strength;
    float u_rainbow_banding;
    float u_ringing_amount;
    float u_ghosting_offset_px;
    int   u_noise_type;
    float u_noise_strength;
    int   u_signal_connection;
    float u_time;
    float _pad0;
    float u_resolution[2];     // vec2
    float _pad1[2];
};
static_assert(sizeof(PassUniforms_PassMinus1) % 16 == 0);

struct PassUniforms_Pass0 {
    float u_dither_detect_threshold;
    float u_resolution[2];
    float _pad0;
};

// ... idem para Pass1..Pass6 — full layout en src/render/pass_uniforms.h
//     después de que el SPEC esté locked.

#pragma pack(pop)

} // namespace tubelight
```

**Invariantes**:
- F1.1: el layout C++ debe coincidir bit-a-bit con el `cbuffer` HLSL
  generado por SPIRV-Cross. Verificación: assert en D3D12Backend cuando
  el tamaño declarado en `create_pass` no concuerda con el reflection
  del DXIL.
- F1.2: padding explícito a múltiplo de 16 bytes. D3D12 lo requiere.
- F1.3: `vec3` debe ir SIEMPRE seguido de un float scalar (`vec3 + f`
  ocupa 16 bytes en HLSL; un `vec3` solo sigue ocupando 16). Si
  declaramos `vec3 a; vec3 b;` HLSL inserta padding entre ellos.
  Capturado por `static_assert`.

### C3c-F2 — Output de build pipeline shader

```
${CMAKE_BINARY_DIR}/shaders/
├── spirv/
│   ├── pass_minus1_signal.spv
│   ├── pass0_analysis.spv
│   ├── ... (8 fragment shaders)
│   └── fullscreen.spv
├── hlsl/
│   ├── pass_minus1_signal.hlsl
│   └── ...
└── dxil/
    ├── pass_minus1_signal.dxil
    ├── ...
    └── fullscreen.dxil
```

Los `.dxil` se copian al runtime dir como `shaders/dxil/*.dxil` junto al
exe (mismo esquema que `shaders/*.frag` ya hace para GL).

## Invariantes globales

- **GI-1**: cualquier shader nuevo añadido a `shaders/pass*.frag` se
  port-ea automáticamente a DXIL en el siguiente build (sin tocar
  CMake). Add-only.
- **GI-2**: el nombre del entry point HLSL es siempre `main` (porque
  GLSL → SPIR-V genera `main` por defecto y SPIRV-Cross lo preserva).
  `dxc -E main` fija.
- **GI-3**: el ordenado de samplers en HLSL es estable porque SPIRV-Cross
  los emite en orden de declaración del GLSL. El root signature D3D12
  asume `u_source : t0, u_prev_frame : t1, u_bezel_tex : t2`.
- **GI-4**: ninguna pasada GLSL puede usar features que SPIRV-Cross no
  traduzca a HLSL 6.0: nada de subgroup ops, nada de atomic ops, nada
  de `gl_HelperInvocation`. Los 8 shaders actuales cumplen — verificado
  por inspección.
