# Plan — Phase 3c (Tubelight)

> 5 fases con gates binarios. Heredan C8 (no cerrar fase sin gate verde).
> Estimación total: 5-7 días sesión agentic, ~1 semana real-clock.

## F3c-1 — Build pipeline GLSL → DXIL (sin tocar runtime)

**Objetivo**: `cmake --build` genera los 8 `.dxil` desde los `.frag`
existentes. Runtime no los usa todavía.

**Tiempo estimado**: 0.5-1 día

**Dependencias**: vcpkg disponible (ya hay). Añadir `glslang` y
`directx-shader-compiler` al manifest.

**Entregable**:
- `vcpkg.json` con `glslang` y `directx-shader-compiler` añadidos.
- `cmake/CompileShaders.cmake` con función
  `tubelight_compile_shaders(<target>)` que registra los 8 ports +
  fullscreen vert.
- `shaders/fullscreen.vert` extraído del `default_fullscreen_vertex_source()`
  de [`src/core/shader.cpp`](src/core/shader.cpp).
- `src/CMakeLists.txt` invoca la función → `tubelight_shaders` custom
  target → `tubelight_core` depende.

**Criterio de salida (gate)**:
- [ ] `cmake --build --target tubelight_shaders` produce 9 `.dxil` no
      vacíos (8 frag + 1 vert) en `build/.../shaders/dxil/`.
- [ ] El hash SHA256 de un mismo `.dxil` es estable en 3 builds
      consecutivos clean (`rm -rf build && cmake ... && build`).
- [ ] CI Windows verde tras añadir los packages a vcpkg.
- [ ] El runtime sigue sin cambios visibles — `--renderer gl` y
      `--renderer dx12` igual que en v0.2.0-alpha.0.

**Decisión post-gate**: continúa a F3c-2.

---

## F3c-2 — `IRenderBackend` v2 + GLBackend implementa nuevos métodos

**Objetivo**: extender la interfaz con los nuevos métodos de
CONTRACTS §C3c-API2 y que `GLBackend` los implemente sin que `Pipeline`
todavía los use. Test unitario sintético los ejercita.

**Tiempo estimado**: 1 día

**Dependencias**: F3c-1.

**Entregable**:
- `src/render/backend.h` con los métodos virtuales añadidos.
- `src/render/handle.h` con los `TextureHandle/RenderTargetHandle/PassHandle`.
- `src/render/pass_uniforms.h` con los 8 structs POD.
- `src/render/backend_gl.cpp` implementando todos los métodos
  (pools internos con `std::unordered_map<uint32_t, Texture2D|FBO|ShaderProgram>`).
- `src/render/backend_d3d12.cpp` con stubs que devuelven `{0}` y warn —
  no se desbloquea `supports_pipeline()` aún.
- Unit test `tests/render/test_gl_handles.cpp` (puede ser un main
  pequeño bajo `TUBELIGHT_BUILD_TESTS`): crea backend, sube textura,
  crea pass, set_uniform_block, draw_fullscreen_quad, lee pixels,
  destruye todo, sin leaks.

**Criterio de salida (gate)**:
- [ ] `cmake --build` sin warnings nuevos.
- [ ] Test unitario `test_gl_handles` verde.
- [ ] Pipeline NO usa los nuevos métodos aún — `--shader-only gl` y
      `dx12` se comportan idéntico a F3c-1.

**Decisión post-gate**: continúa a F3c-3.

---

## F3c-3 — Pipeline migra a handles (GL-only)

**Objetivo**: `Pipeline` deja de tocar `FBO`/`ShaderProgram`/`Texture2D`
directamente. Toda comunicación va por handles + backend. GL backend
sigue siendo el único que las resuelve.

**Tiempo estimado**: 1-2 días

**Dependencias**: F3c-2.

**Entregable**:
- `src/core/pipeline.h` con `rt_handles_[]`, `pass_handles_[]`,
  `history_rt_`, `history_tex_`, `bezel_tex_`. Quitar miembros
  GL-concretos (movidos a pools en GLBackend).
- `src/core/pipeline.cpp::render_to_screen` reescrita usando handles +
  backend methods (ver flujo en DESIGN §"Flujo de datos").
- `src/core/pipeline.cpp::create` también — registra los recursos
  contra el backend.
- `src/core/pipeline.cpp::apply_uniforms_for_pass` reescrita: rellena
  el struct `PassUniforms_N` correspondiente, llama
  `backend_->set_uniform_block`. El switch sobre pass_index se mantiene.
- `Pipeline::render_to_screen(GLuint)` → `render_to_screen(TextureHandle)`.
- `run_shader_only` en main.cpp adaptado: `Texture2D source` ya carga
  desde disco, pasar `source.id()` (== handle.id ahora que GLBackend
  asigna IDs consistentemente).

**Criterio de salida (gate)**:
- [ ] `--shader-only --renderer gl` produce output **byte-exacto** al
      baseline `tests/golden/gl_v017_baseline.png` (M2 del SPEC).
- [ ] Cero cambios visibles en GL path (smoke manual con 3 profiles
      diferentes).
- [ ] DX12 path sigue siendo el clear azul de 3b (`supports_pipeline()
      == false` todavía).

**Decisión post-gate**: continúa a F3c-4. Si gate falla, bisect del
diff de Pipeline — algo cambió en el orden / formato GL.

---

## F3c-4 — D3D12Backend implementa los handles + PSO + ejecución

**Objetivo**: D3D12 carga los `.dxil`, crea PSOs por pasada, ejecuta el
ciclo completo. Output visual igual a GL en cualquier testcard +
profile.

**Tiempo estimado**: 2-3 días

**Dependencias**: F3c-3.

**Entregable**:
- `src/render/backend_d3d12.cpp` expande las implementaciones stub:
  - `create_texture`: `CreateCommittedResource` DEFAULT + staging UPLOAD
    en `upload_texture_rgba8`.
  - `create_render_target`: `CreateCommittedResource` ALLOW_RENDER_TARGET
    + RTV en heap dedicado + SRV en heap CBV/SRV/UAV (shader-visible).
  - `create_pass`: cargar `.dxil`, crear root signature (1 CBV b0 + 3
    SRVs t0-t2 + 3 samplers s0-s2 estáticos linear-clamp), crear PSO
    con fullscreen.dxil VS + el pass FS.
  - `bind_pass`: SetPipelineState + SetGraphicsRootSignature +
    SetDescriptorHeaps.
  - `bind_texture`: SetGraphicsRootDescriptorTable al slot
    correspondiente.
  - `set_uniform_block`: memcpy a un ring buffer constant buffer + bind
    via SetGraphicsRootConstantBufferView.
  - `copy_rt_to_texture`: barrier RTV→COPY_SOURCE, CopyTextureRegion,
    barrier de vuelta.
- `D3D12Backend::supports_pipeline()` → `return true;`.
- `Pipeline::create` deja de rechazar el backend.

**Criterio de salida (gate)**:
- [ ] `tubelight --renderer dx12 --shader-only testcard.png --profile pvm-8220 --signal composite_ntsc`
      produce output visible CRT (no clear azul).
- [ ] Smoke manual: 3 profiles (pvm-8220, fw900, terminal-p31) × 2
      signals (composite_ntsc, rgb_vga) → 6 outputs DX12 visualmente
      iguales a GL.

**Decisión post-gate**: continúa a F3c-5. Si el gate falla por bug en
una sola pasada, aislar con `--pass-only N` (flag debug nuevo si no
existe) y depurar.

---

## F3c-5 — Pixel-equivalence gate + release v0.2.0-beta

**Objetivo**: M1 del SPEC verde + tag + release notes + push.

**Tiempo estimado**: 0.5-1 día

**Dependencias**: F3c-4.

**Entregable**:
- `tests/golden/dx12_vs_gl_psnr.py` (Pillow + numpy):
  carga `gl_pvm-8220_composite_ntsc.png` y `dx12_pvm-8220_composite_ntsc.png`,
  calcula PSNR + Δ max per-canal, exit 0 si pasa, 1 si no.
- `tests/golden/capture_baseline.ps1` (PowerShell): script que invoca
  el binario en ambos modos y guarda los PNGs.
- CI workflow `pixel_equivalence.yml`: corre el script + el python en
  cada push a `main` que toque `src/render/` o `shaders/`.
- CHANGELOG entry v0.2.0-beta documentando PSNR alcanzado.
- Tag v0.2.0-beta, build zip, GH release via release-publisher.

**Criterio de salida (gate)**:
- [ ] M1 verde: PSNR ≥ 40 dB, Δ ≤ 4/255 sobre pvm-8220+composite_ntsc.
- [ ] M2 verde: hash GL byte-exacto al baseline.
- [ ] M3 verde: hash DXIL determinista en 3 builds.
- [ ] M6 verde: pr-reviewer sin P0/P1.
- [ ] Zip release ≤ 60 MB (sin DLLs dxc, solo bytecode).
- [ ] Tag v0.2.0-beta empujado.

**Decisión post-gate**: cierra Phase 3c. Abre Phase 3d (WGC capture).
