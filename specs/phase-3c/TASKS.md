# Tasks — Phase 3c (Tubelight)

> Granularidad S/M/L. DoD binario. Sin verbos vagos.

## F3c-1 — Build pipeline GLSL → DXIL

- [ ] **T1.1 (S)** — Añadir `glslang` y `directx-shader-compiler` a
      `vcpkg.json` —
      DoD: `vcpkg install --triplet x64-windows` los baja sin error.
- [ ] **T1.2 (S)** — Extraer `default_fullscreen_vertex_source()` de
      `src/core/shader.cpp` a `shaders/fullscreen.vert` —
      DoD: archivo existe, contenido idéntico, `shader.cpp` lo lee con
      `build_from_files`.
- [ ] **T1.3 (M)** — Escribir `cmake/CompileShaders.cmake` con función
      `tubelight_compile_shaders(<target>)` que registra los 9 ports —
      DoD: invocada en `src/CMakeLists.txt`, crea custom target
      `tubelight_shaders`, dependencia añadida a `tubelight_core`.
- [ ] **T1.4 (S)** — Configurar copia post-build de `*.dxil` al
      runtime dir junto al exe —
      DoD: `build/.../Release/shaders/dxil/*.dxil` existe.
- [ ] **T1.5 (S)** — Verificar determinismo: 3 builds clean, diff de
      hashes SHA256 vacío —
      DoD: log de hashes pegado en CHANGELOG.

## F3c-2 — IRenderBackend v2 + GLBackend

- [ ] **T2.1 (S)** — Crear `src/render/handle.h` con los 3 structs
      handle + `PixelFormat` enum + `TextureDesc` + `PassDesc` —
      DoD: compila standalone.
- [ ] **T2.2 (M)** — Crear `src/render/pass_uniforms.h` con los 8
      structs POD + `static_assert(sizeof(...) % 16 == 0)` —
      DoD: incluye en GLBackend test y compila.
- [ ] **T2.3 (M)** — Extender `IRenderBackend` con los 11 métodos
      virtuales nuevos (CONTRACTS §C3c-API2) —
      DoD: compila; backends concretos rotos (esperado).
- [ ] **T2.4 (L)** — Implementar los 11 métodos en `GLBackend` usando
      pools internos `unordered_map<uint32_t, …>` —
      DoD: el unit test T2.6 verde.
- [ ] **T2.5 (S)** — Stubs vacíos para `D3D12Backend` (devuelven `{0}`,
      warn una vez) —
      DoD: compila; `supports_pipeline()` sigue false.
- [ ] **T2.6 (M)** — Unit test `tests/render/test_gl_handles.cpp` que
      ejercita create/upload/bind/draw/destroy en GL —
      DoD: corre bajo `TUBELIGHT_BUILD_TESTS=ON`, exit 0, sin GL errors.

## F3c-3 — Pipeline migra a handles

- [ ] **T3.1 (M)** — Capturar baseline GL: `--shader-only testcard.png
      --profile pvm-8220 --signal composite_ntsc` →
      `tests/golden/gl_v017_baseline.png` + SHA256 commit'eado —
      DoD: archivo + checksum existen.
- [ ] **T3.2 (M)** — Refactor `Pipeline` miembros: quitar `shaders_`,
      `fbos_`, `history_fbo_`, `bezel_image_`, añadir
      `pass_handles_`, `rt_handles_`, `history_rt_`, `history_tex_`,
      `bezel_tex_` —
      DoD: header compila.
- [ ] **T3.3 (L)** — Reescribir `Pipeline::create` usando
      `backend_->create_*` —
      DoD: GL backend instancia los 8 RTs + 8 passes + 1 history sin
      error.
- [ ] **T3.4 (L)** — Reescribir `apply_uniforms_for_pass`: rellena
      el struct POD correspondiente, llama `set_uniform_block` —
      DoD: los 8 switch cases existen y compilan.
- [ ] **T3.5 (L)** — Reescribir `Pipeline::render_to_screen`
      cambiando `GLuint source_tex` por `TextureHandle source`, usando
      backend methods para bind/clear/viewport/draw —
      DoD: el ciclo loop compila + corre.
- [ ] **T3.6 (S)** — Actualizar `run_shader_only` y
      `overlay_mode_win.cpp` para pasar handle en vez de GLuint —
      DoD: ambos compilan.
- [ ] **T3.7 (M)** — Smoke test: GL output == baseline (M2 verde) —
      DoD: SHA256 idéntico o ε = 0.

## F3c-4 — D3D12Backend ejecuta pipeline

- [ ] **T4.1 (M)** — Implementar `D3D12Backend::create_texture` +
      `upload_texture_rgba8` con staging upload —
      DoD: unit test sintético sube 256×256 texture, lee back, compara.
- [ ] **T4.2 (M)** — Implementar `create_render_target` (CCRTresource +
      RTV + SRV) —
      DoD: unit test crea RT, clear, lee back azul.
- [ ] **T4.3 (L)** — Root signature común: 1 CBV(b0) + 3 SRVs(t0-t2) +
      3 samplers estáticos linear-clamp (s0-s2) —
      DoD: D3D12_ROOT_SIGNATURE_DESC construido, serializado sin error.
- [ ] **T4.4 (L)** — `create_pass`: carga `.dxil`, crea PSO con
      fullscreen.dxil VS + el pass FS, blend disabled, depth disabled —
      DoD: 8 PSOs creados, sin runtime validation errors (con debug
      layer ON).
- [ ] **T4.5 (M)** — `bind_pass` + `bind_texture` +
      `set_uniform_block` (ring CB upload heap) —
      DoD: secuencia bind→set_uniforms→draw produce el clear coloreado
      esperado en unit test minimal.
- [ ] **T4.6 (M)** — `copy_rt_to_texture` con barriers correctos
      (RTV→COPY_SOURCE / COPY_DEST→SRV) —
      DoD: history snapshot funcional en smoke pass 5.
- [ ] **T4.7 (S)** — `supports_pipeline()` → `true` y main.cpp ya no
      necesita el path proof-of-life DX12 separado —
      DoD: `run_shader_only_dx12` se simplifica o se borra.
- [ ] **T4.8 (M)** — Smoke manual 6 outputs (3 profiles × 2 signals) GL
      vs DX12 visualmente comparables —
      DoD: screenshots side-by-side en `tests/golden/` (manual review).

## F3c-5 — Pixel-equivalence gate + release

- [ ] **T5.1 (S)** — Script `tests/golden/capture_baseline.ps1` que
      lanza el binario en `--shader-only --screenshot <path>` para
      ambos backends —
      DoD: produce 2 PNGs en `tests/golden/`.
- [ ] **T5.2 (M)** — Añadir flag `--screenshot <path>` a `--shader-only`
      (renderiza 60 frames, captura el frame 60, exit) —
      DoD: PNGs generados son válidos y reproducibles.
- [ ] **T5.3 (M)** — Script `tests/golden/dx12_vs_gl_psnr.py`
      (Pillow + numpy): PSNR + Δ max, exit 0 si pasa —
      DoD: corre con `python -m pytest` o standalone, > 40 dB.
- [ ] **T5.4 (S)** — CI workflow `.github/workflows/pixel_equivalence.yml`
      que invoca capture + python en cada push relevante —
      DoD: yml válido, primer run verde.
- [ ] **T5.5 (S)** — CHANGELOG v0.2.0-beta documentando PSNR alcanzado,
      tamaño zip, perfiles testeados —
      DoD: entry existe, formato Keep a Changelog.
- [ ] **T5.6 (S)** — Tag v0.2.0-beta, build zip via release-publisher,
      push, GH release —
      DoD: release público visible, zip ≤ 60 MB.
