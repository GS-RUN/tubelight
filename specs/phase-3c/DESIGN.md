# Design — Phase 3c (Tubelight)

## Arquitectura general

```
                        Pipeline (CORE, único)
                      ┌──────────────────────────┐
                      │ - parámetros / profiles  │
                      │ - flags enabled[]        │
                      │ - history snapshot logic │
  source: TextureH ──▶│ - orquestación 8 passes  │──▶ swap-chain
                      │                          │
                      │ usa solo handles:        │
                      │   TextureHandle          │
                      │   RenderTargetHandle     │
                      │   PassHandle             │
                      └────────────┬─────────────┘
                                   │ (todas las llamadas son virtual)
                ┌──────────────────┴───────────────────┐
                ▼                                       ▼
      ┌────────────────┐                    ┌─────────────────┐
      │   GLBackend    │                    │   D3D12Backend  │
      │  resuelve a:   │                    │  resuelve a:    │
      │  - GLuint tex  │                    │  - ID3D12Resrc  │
      │  - GLuint fbo  │                    │  - DSV/RTV/SRV  │
      │  - GLSL prog   │                    │  - PSO + ROOT   │
      └────────┬───────┘                    └────────┬────────┘
               │                                     │
               ▼                                     ▼
      shaders/passN.frag                  shaders/build/dxil/passN.dxil
      (loaded at runtime,                 (generated at build time via
       glCompileShader)                    glslang→SPIRV-Cross→dxc)
```

## Componentes

### Pipeline (core, **NO replaceable**)
Orquesta las 8 pasadas. Solo conoce el backend a través de
`IRenderBackend`. Conserva los handles devueltos por `create_*()`.
Llama a `bind_*()` + `set_uniform_*()` + `draw_fullscreen_quad()` en el
orden definido por su lógica histórica (snapshot de pass 5, skip
pass 5, identity passes).

### IRenderBackend (interfaz, ya existe — extendida en 3c)
Hereda de la versión Phase 3a/3b. Métodos nuevos:

```cpp
TextureHandle      create_texture(const TextureDesc&);
RenderTargetHandle create_render_target(int w, int h, PixelFormat);
PassHandle         create_pass(int pass_index, const PassDesc&);
void               destroy_texture(TextureHandle);
void               destroy_render_target(RenderTargetHandle);
void               destroy_pass(PassHandle);

void               upload_texture_rgba8(TextureHandle, const void* data, int w, int h);
void               copy_rt_to_texture(RenderTargetHandle src, TextureHandle dst);

void               bind_render_target(RenderTargetHandle);   // null = default
void               bind_pass(PassHandle);
void               bind_texture(int slot, TextureHandle);
void               set_uniform_block(PassHandle, const void* data, size_t bytes);
```

Los handles son opaque (`struct TextureHandle { uint32_t id; }`).
Lifecycle: el backend dueño. Pipeline mantiene IDs pero el backend
libera GPU memory en `shutdown()`.

### GLBackend [REPLACEABLE: no — único backend portable Linux]
Implementa los nuevos métodos enrutando al código existente:
- `create_texture()` → wrapper interno sobre `Texture2D` movido a un pool
  indexado por `id`.
- `create_pass()` → `ShaderProgram::build_from_files(default_vert, pass_<N>.frag)`.
- `set_uniform_block()` → memcpy a un buffer staging + cuando bind_pass
  cambia, glUniform* per-campo según el layout declarado.

### D3D12Backend [REPLACEABLE: para Vulkan en v1.x si Linux gana presencia]
Implementa los nuevos métodos sobre D3D12:
- `create_texture()` → `CreateCommittedResource` con `D3D12_HEAP_TYPE_DEFAULT`,
  upload via staging `D3D12_HEAP_TYPE_UPLOAD` (en `upload_texture_rgba8`).
- `create_render_target()` → `CreateCommittedResource` formato `R16G16B16A16_FLOAT`
  con flag `ALLOW_RENDER_TARGET`. Crea RTV en heap dedicado + SRV en
  heap CBV/SRV/UAV shader-visible.
- `create_pass()` → carga `.dxil` desde
  `${CMAKE_BINARY_DIR}/shaders/dxil/pass_<N>.dxil`, crea PSO con la VS
  fullscreen triangle (también compilada de la default GLSL vert),
  root signature con: 1 CBV (b0, uniforms) + N SRVs + N samplers.
- `set_uniform_block()` → memcpy a un ring de constant buffers
  upload-heap, bind el CBV de ese slot.

### ShaderBuild (sub-sistema build-time, nuevo)
Script CMake `cmake/CompileShaders.cmake` que:
1. Por cada `shaders/pass*.frag`:
   - `glslangValidator -V -S frag --target-env vulkan1.0 -o build/spirv/<pass>.spv`
   - `spirv-cross --hlsl --shader-model 60 build/spirv/<pass>.spv --output build/hlsl/<pass>.hlsl`
   - `dxc -T ps_6_0 -E main -Fo build/dxil/<pass>.dxil build/hlsl/<pass>.hlsl`
2. Para el vertex shader fullscreen-triangle (extraído de `shader.cpp`
   a `shaders/fullscreen.vert`):
   - Mismo flujo, `-S vert` y `-T vs_6_0`.
3. Custom target `tubelight_shaders` que `tubelight_core` depende.

Salida: `${CMAKE_BINARY_DIR}/shaders/dxil/*.dxil` que se copian al
runtime dir junto al exe.

## Decisiones técnicas (con evidencia citada)

### D3c-1 — Handles opacos por ID en vez de interfaces
**Decisión**: `struct TextureHandle { uint32_t id; bool is_valid() const; };`
con namespace de IDs interno al backend. Pipeline solo guarda IDs;
backend hace lookup en su pool.
**Alternativas descartadas**:
- A: Interfaces (`ITexture* tex`) — requiere herencia múltiple cuando
  Pipeline necesita lifecycle (RAII handle vs raw pointer fricción).
- B: Variants (`std::variant<GLuint, ID3D12Resource*>`) — fuga el
  detalle del backend a Pipeline. Viola la abstracción.
- C: Templates (`Texture<Backend>`) — Pipeline ya no podría tener
  arrays homogéneos sin más metaprogramming.
**Evidencia**: bgfx (https://github.com/bkaradzic/bgfx) usa IDs opacos
(`bgfx::TextureHandle`) por exactamente esta razón. The Forge igual.
Es el patrón estándar de abstracciones de render multi-API.

### D3c-2 — Layout de uniforms unificado per-pass
**Decisión**: cada pasada declara un `struct PassUniforms_N` POD en
`src/render/pass_uniforms.h`. Pipeline construye una instancia en
stack, `set_uniform_block(handle, &uniforms, sizeof(uniforms))`. GL la
desempaqueta a glUniform*; D3D12 la mapea a un constant buffer.
**Alternativas descartadas**:
- A: Reflexión (`ShaderProgram::set_float("u_xxx", v)` per-frame) —
  funciona en GL pero D3D12 necesitaría introspectar el DXIL en cada
  bind. Lento y frágil.
- B: Auto-generación desde el GLSL — requiere parser GLSL custom o
  glslang reflection API. Bloqueante; lo dejamos a Phase 7a (Slang).
**Evidencia**: ya tenemos los uniforms enumerados en
[`pipeline.cpp:56-147`](src/core/pipeline.cpp:56) — convertirlos a 8
structs POD es mecánico, ~150 líneas.

### D3c-3 — Backbuffer R8G8B8A8_UNORM, FBOs intermedios R16G16B16A16_FLOAT
**Decisión**: D3D12 swap chain mantiene `R8G8B8A8_UNORM` (lo de 3b).
Los 7 FBOs intermedios usan `DXGI_FORMAT_R16G16B16A16_FLOAT` igual que
GL (`GL_RGBA16F`).
**Alternativas descartadas**:
- A: scRGB FP16 backbuffer (HDR-ready) — requiere swap chain
  `R16G16B16A16_FLOAT` + `DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709`,
  flujo HDR10 metadata. Es Phase 5a y bloquea sobre R14.
- B: 8-bit intermedios — bloom y beam pass desbordan a 1.0 sin headroom.
  Sería regresión visual.
**Evidencia**: `core/fbo.h:23` ya lo documenta — "Default RGBA16F gives
HDR-style intermediate storage which is necessary for linear-space
passes". Igual en DX12.

### D3c-4 — DXIL precompilado en zip, no compile-on-load
**Decisión**: el zip de release incluye `shaders/dxil/*.dxil`. El runtime
los carga via `D3DReadFileToBlob`. NO se incluye `dxcompiler.dll` ni
`dxil.dll` en el release.
**Alternativas descartadas**:
- A: Compile-on-load via DXC — añade ~50 MB de DLLs al zip
  (`dxcompiler.dll` + `dxil.dll`), latencia de startup +200 ms, riesgo
  de hash mismatch si dxc difiere en la máquina de build vs runtime.
- B: HLSL en disco + compilación on-load con `D3DCompile` (fxc API) —
  fxc no soporta SM 6.0+; restringe features futuras.
**Evidencia**: ADR-0002 §"Rompe" — "shippeamos bytecode precompilado, no
DXC, tamaño zip se mantiene ~55-60 MB". Misma decisión, formalizada aquí.

### D3c-5 — Pixel-equivalence harness en Python (no C++)
**Decisión**: `tests/golden/dx12_vs_gl_psnr.py` usa `Pillow` + `numpy`
para cargar dos PNGs y calcular PSNR + Δ max per-canal. CI invoca el
binario en modo `--shader-only --screenshot <path>` para ambos backends
y luego corre el script. Falla si PSNR < 40 dB o Δ > 4/255.
**Alternativas descartadas**:
- A: Harness C++ con `stb_image` — más LOC, peor para iterar el umbral,
  no aprovecha numpy.
- B: ImageMagick CLI `compare -metric PSNR` — añade dep de sistema; el
  proyecto ya tiene Python en `tools/`.
**Evidencia**: `tools/precommit_check_paths.sh` (introducido en v0.1.2)
ya establece el patrón "tooling Python para guards de CI".

## Flujo de datos

```
build time:
  shaders/pass<N>.frag
    └→ glslang -V -S frag → shaders/build/spirv/pass<N>.spv
        └→ spirv-cross --hlsl --shader-model 60 → shaders/build/hlsl/pass<N>.hlsl
            └→ dxc -T ps_6_0 -E main → shaders/build/dxil/pass<N>.dxil
  shaders/fullscreen.vert (extraído de shader.cpp default_fullscreen_vertex_source)
    └→ glslang -V -S vert → ...build/dxil/fullscreen.dxil

runtime D3D12 path:
  Pipeline::create()
    backend.init({hwnd, w, h})
    for N in 0..7:
      backend.create_pass(N, {load("dxil/pass<N>.dxil"), load("dxil/fullscreen.dxil")})
    backend.create_texture(testcard data)
    for N in 0..6:
      backend.create_render_target(w, h, R16G16B16A16_FLOAT)
    backend.create_render_target(w, h, R16G16B16A16_FLOAT)  // history

  Pipeline::render_to_screen(source_tex):
    backend.begin_frame()
    for N in 0..7:
      if skip: continue
      backend.bind_render_target(fbo_N or null)
      backend.set_viewport(0,0,w,h)
      backend.clear_color(0,0,0,1)
      backend.bind_pass(pass_N)
      backend.bind_texture(0, current_input)
      if N == 6: backend.bind_texture(1, history_fbo_tex)
      if N == 7: backend.bind_texture(2, bezel_tex)
      backend.set_uniform_block(pass_N, &uniforms_N, sizeof)
      backend.draw_fullscreen_quad()
      if N == 6: backend.copy_rt_to_texture(fbo_5, history_fbo)
      current_input = fbo_N.texture
    backend.end_frame()
```

## Estado y persistencia

Nada nuevo persistido en disco. Los `.dxil` son artefactos de build;
ya cubiertos por `.gitignore` (build/ está ignorado). El zip de release
los incluye bajo `shaders/dxil/`.

## Replaceable parts

- **`D3D12Backend`** [REPLACEABLE] — `VulkanBackend` futura puede
  implementar la misma `IRenderBackend` v2. La forma de los handles +
  el contrato de `set_uniform_block` están diseñados pensando en CBV
  Vulkan también.
- **`cmake/CompileShaders.cmake`** [REPLACEABLE] — sustituible por
  `slangc` en Phase 7a (single-source). Pipeline no lo sabe.
- **`tests/golden/dx12_vs_gl_psnr.py`** [REPLACEABLE] — el umbral 40 dB
  y la métrica PSNR son tuning. Si queremos SSIM o Δ max ponderado
  perceptualmente, se cambia sin tocar nada más.
