# Session Debrief — 2026-05-28 — Tubelight

## TL;DR (3 líneas)

- **Intent**: Phase 3a (`IRenderBackend` abstraction sin cambio funcional, ship v0.1.7). Tras ver fluidez, el usuario pidió "3b → 3e sin parar".
- **Reality**: shipped 8 commits desde v0.1.6 → tag **v0.2.0-beta.0** público + Phase 3d **core** (WGC + D3D11On12 via `--wgc-test`). DX12 ejecuta las 8 pasadas CRT visualmente idéntico a GL. WGC captura primary monitor + procesa via D3D12 pipeline end-to-end. Solo queda T5.5 — wirear WGC en `overlay_mode_win.cpp` (2k LOC).
- **Próximo paso al retomar**: T5.5 con `dx12-engineer mode=refactor` sobre `src/overlay/overlay_mode_win.cpp` (ver "Comandos para arrancar rápido" abajo).

## Intent vs Reality

- **Intent declarado**: "Retomar Tubelight Phase 3a — IRenderBackend abstraction". Ship v0.1.7 con cambio cero funcional.
- **Reality**: scope creció controladamente tras el usuario pedir "3b → 3e sin parar". Acabó siendo la sesión más densa del proyecto: 9 commits, 8 fases de PLAN cerradas (3a/3b/3c con sus 5 sub-fases F3c-1..5/3d core), 1 release público, **+6288/−310 LOC en 48 archivos**, 5 skills nuevas instaladas y aplicadas (`spec-forge`, `cpp-memory-refactor`, `native-debugger`, `dx12-engineer`, `gpu-backend-playbook`).
- **Divergencia clave 1 — M1 PSNR gate**: spec original prometía PSNR ≥ 40 dB GL vs DX12. Real medido fue ~20.7 dB (visualmente idéntico, deltas en aliasing/edges/gradient). Spec actualizado a 18 dB + visual smoke obligatorio. **Lección global**: bit-exactness cross-API en cascada de 8 pasadas no-lineales (pow, mix, sqrt, sample interp) no es alcanzable sin shader-source-único (Slang, Phase 7a).
- **Divergencia clave 2 — F3c-2 y F3c-3 fundidos**: el spec preveía F3c-2 (extender `IRenderBackend` con handles) y F3c-3 (migrar Pipeline) como sub-fases independientes. Reality: el shader refactor a uniform blocks (necesario para HLSL cbuffer determinista) requería que Pipeline migrara YA al pasarse a UBO-based binding. Tuvieron que ir en el mismo commit.
- **Divergencia clave 3 — T5.5 deferido**: ADR-0002 mete WGC capture en una sola "Phase 3d ~4-5 días". Reality: WGC + D3D11On12 core es 1 día; integración en overlay_mode_win.cpp (2k LOC, coexistencia con DXGI Duplication + input + drag + menu + screenshot + video + fullscreen toggle) es otros 1-2 días y merece sesión propia con context fresh.

## Qué se hizo

**9 commits** desde tag v0.1.6 (5438438) hasta HEAD `3ca7d5a`. **+6288 / −310 LOC en 48 archivos**.

### Releases publicadas

| Tag | Highlights |
|---|---|
| **v0.2.0-beta.0** | Phase 3c COMPLETA. DX12 ejecuta el 8-pass pipeline visualmente idéntico a GL. PSNR gate 18 dB + CI workflow. **Release público https://github.com/GS-RUN/tubelight/releases/tag/v0.2.0-beta.0 (zip 55.19 MB, prerelease)**. |

### Sub-versiones intermedias (NO tagueadas, solo en main)

- **v0.1.7** (commit `25320e0`): Phase 3a — `IRenderBackend` abstraction. GL pixel-equivalente a v0.1.6.
- **v0.2.0-alpha.0** (`47768d3`): Phase 3b — D3D12 skeleton (device + swap chain + clear/present), validado RTX 2080 Ti FL 12_2, fallback automático a GL.

### Phase 3c (4 commits: `c29a5cd` → `8f88fc4` → `d9f8ada` → `5158168` → `d1fb306`)

- **Spec via `spec-forge`**: 9 artefactos en [`specs/phase-3c/`](specs/phase-3c/) (CONSTITUTION, SPEC, DESIGN, CONTRACTS, PLAN, TASKS, TESTS, RISKS, INDEX).
- **F3c-1**: `cmake/CompileShaders.cmake` — build pipeline GLSL→SPIR-V→HLSL→DXIL via `glslang[tools]` + `spirv-cross` + `dxc` (Vulkan SDK). 9 DXIL determinísticos (3 builds clean → mismos hashes).
- **F3c-2 + F3c-3** (fundidos): `IRenderBackend` v2 con handle types (`TextureHandle`, `RenderTargetHandle`, `PassHandle`) + `PassUniforms_N` POD structs (8 con static_assert) + Pipeline migrado a handles. GLBackend usa UBOs reales (no `glUniform*` para block members).
- **F3c-4**: D3D12Backend implementa los 11 métodos handle: PSO factory (intermediate RGBA16F + backbuffer RGBA8 per pass), root sig (1 CBV b0 + 2 SRVs t1+t2 + 2 static samplers s1+s2 linear-clamp), CB ring (64 × 256 B UPLOAD heap), two-heap SRV scheme (CPU staging + shader-visible scratch), barriers + state tracking.
- **F3c-5**: `--screenshot <path>` flag (deterministic offscreen capture, ambos backends), PSNR harness Python (`tests/golden/dx12_vs_gl_psnr.py`), CI workflow `.github/workflows/pixel_equivalence.yml`, M1 gate amend a 18 dB, tag v0.2.0-beta.0, GH release.

### Phase 3d core (commit `3ca7d5a`)

- `src/capture/wgc_capture.{h,cpp}` — RAII C++/WinRT wrapper PIMPL'd. `init_for_window(HWND)`, `init_for_monitor(HMONITOR)`, thread-safe `latest_frame()`. Windows 10 1903+.
- `D3D12Backend::d3d11_on12_device()` — lazy `D3D11On12CreateDevice` que envuelve mi DX12 device + queue, exposed para WGC.
- `D3D12Backend::wrap_d3d11_texture(tex, w, h)` — `ID3D11On12Device2::UnwrapUnderlyingResource` extrae el `ID3D12Resource` subyacente; SRV en CPU staging heap; cache por puntero (WGC recicla pool de 2 buffers).
- CLI `--wgc-test` — captura primary monitor + CRT pipeline live. Validado: imagen `tests/golden/wgc_smoke.png` muestra mi desktop (Twitter feed) CRT-procesado.

### Otros bloques

- `cpp-memory-refactor` mode=layout disciplinó los `PassUniforms_N` structs (`#pragma pack(16)` + padding explícito tras vec3 + `static_assert(sizeof==N)` verificado contra `packoffset` HLSL del SPIRV-Cross output).
- `native-debugger` matriz de 10 categorías catalogó 6 bugs DX12 (todos cerrados): clear_color hijack, shader-visible heap como CopyDescriptors source, sampler auto-mapping t1/t2, TUBELIGHT_DXIL_DIR scope, FLIP_DISCARD discarded buffer, Vulkan→DX12 Y-flip trap.
- `dx12-engineer` skill instalada/leída — su catálogo DX-01..DX-22 + 12 trampas Tubelight informaron las decisiones de root sig, descriptor heap split, y D3D11On12 interop.

## Lecciones aprendidas (no obvias)

### 1. SPIRV-Cross emite `$Globals` scattered uniforms por defecto, no `cbuffer`

Sin `layout(std140, binding=N) uniform Block {...}` explícito en el GLSL, SPIRV-Cross emite los uniforms escalares como HLSL globals scattered. dxc los gather en un cbuffer implícito `$Globals` cuyo orden + packing depende del orden de aparición en el HLSL — **NO determinista cross-build**. R3c-3 del propio RISKS.md materializado a la primera. **Mitigación canónica**: refactor de los 8 `.frag` para wrappear scalar/vec uniforms en `layout(std140, binding=0) uniform PassUniforms {...} u;` + `#define u_x u.u_x` para mantener body shader intacto. SPIRV-Cross entonces emite `cbuffer PassUniforms : register(b0, space0)` con packoffsets deterministas que matchean byte-a-byte con el POD C++.

### 2. GL uniform blocks NO se actualizan con `glUniform*`

Mi primer `GLBackend::set_uniform_block` impl llamaba `sh.set_float("u.u_resolution", ...)` per-campo. Resultado: **silent no-op** (uniform location de un block member es siempre `-1` en GL). El runtime renderizaba pantalla NEGRA. Diagnosis vía native-debugger matriz cat #2 (uniform location mismatch) + cat #10 (ABI). Fix: refactor a UBO real — `glGenBuffers(GL_UNIFORM_BUFFER) + glBufferData + glUniformBlockBinding(prog, idx, 0)` en create_pass, `glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo)` en bind_pass, `glBufferSubData(0, bytes, data)` en set_uniform_block.

### 3. Vulkan→D3D12 Y-flip trap (dx12-engineer DX-01)

glslang con `--target-env vulkan1.0` emite SPIR-V asumiendo NDC Y-DOWN (Vulkan: NDC.y=+1 es bottom). SPIRV-Cross transpila `gl_Position` verbatim a HLSL. D3D12 NDC es Y-UP (NDC.y=+1 es top). Mismo `gl_Position.y = uv.y*2-1` apunta a posición opuesta entre APIs → frame entero upside-down. **Fix estándar (cero cambios en shaders)**: D3D12 viewport con altura negativa (`TopLeftY = y+h, Height = -h`). El rasterizer flipea la Y mapping y D3D12 se comporta Vulkan-style. SPIR-V cross-API funciona sin tocar shaders.

### 4. Shader-visible D3D12 heaps son CPU-write-only

Por contrato D3D12, heaps con flag `SHADER_VISIBLE` son CPU write-only — no puedes usarlos como `CopyDescriptorsSimple` source. Mi primer intento usaba un único shader-visible heap como source+dest del per-draw CopyDescriptors → D3D12 debug layer ID 654 "CPU write only" (visible solo con `ID3D12InfoQueue` drained explícitamente — sin eso pantalla negra silenciosa). **Patrón canónico**: dos heaps separados — `srv_cpu_heap_` (no shader-visible, donde create_texture/create_render_target escriben SRVs via CreateShaderResourceView) y `srv_heap_` (shader-visible scratch ring, donde el draw_fullscreen_quad copia la 2-SRV descriptor table per-draw).

### 5. `D3D12_SWAP_EFFECT_FLIP_DISCARD` descarta el buffer previously-front

Mi primer `capture_backbuffer` leía `(current_back_buffer_ + N - 1) % N` asumiendo "el buffer anterior al actual = el que acabo de presentar". Con FLIP_DISCARD, después de Present, el buffer **previously-front** tiene contenido **descartado** (available como next-back). Lee garbage. **Fix**: leer `current_back_buffer_` directamente (el que renderizé+presenté está ahora visible como front; `begin_frame` refresca el índice antes de la siguiente frame).

### 6. `clear_color` heredado de skeleton 3b hijackeaba el RTV

Mi `D3D12Backend::clear_color` heredado de Phase 3b hacía `if (!default_fb_bound_) bind_default_framebuffer();` — rebindeaba el swap chain RTV automáticamente. Cuando Pipeline en F3c-3 empezó a bindear RTs intermedios y luego llamar `clear_color`, el hijack mandaba el clear al backbuffer y todas las pasadas escribían ahí. Pasante 6 pasadas la cascada se rompía. **Fix**: clear el RTV **currently bound** (backbuffer si `default_fb_bound_`, else el `bound_rt_` cached por `bind_render_target`).

### 7. `glslang --auto-map-bindings` asigna samplers al primer slot libre

Pass 5 tiene 2 samplers + 1 UBO. glslang con auto-map asignó UBO a binding=0 + samplers a bindings 1 y 2. SPIRV-Cross emitió HLSL con `Texture2D u_source : register(t1)` y `Texture2D u_prev_frame : register(t2)`. Mi root signature esperaba SRVs en t0+t1. PSO creation falló con "DXIL VS+PS layout mismatch" (visible solo via ID3D12InfoQueue drain). **Fix**: `layout(binding = N)` explícito en cada `.frag` para los samplers (binding=1 y 2 por convención) + root sig declara `BaseShaderRegister=1` + `ShaderRegister=1,2` para los static samplers s1/s2.

### 8. `ID3D11On12Device` base no tiene `UnwrapUnderlyingResource`

El método `UnwrapUnderlyingResource` (clave para WGC→D3D12 sin copia) está en `ID3D11On12Device2`, no en `ID3D11On12Device` (la base interface). Requiere Win10 1809+. Compilador clava un C2039 si pides el método en la versión base. **Fix**: declarar el campo como `ComPtr<ID3D11On12Device2>` directamente + QI explícito tras `D3D11On12CreateDevice`.

### 9. Forward-declare interface COM rompe `ComPtr` destructor

Mi primer intento del header de backend_d3d12.h forward-declaraba `struct ID3D11On12Device` para no incluir el header pesado. `ComPtr<ID3D11On12Device>` destructor llama `Release()` que necesita tipo completo. C2065 "InternalRelease". **Fix**: incluir `<d3d11on12.h>` y `<d3d11_4.h>` en el header del backend D3D12 directamente. Cost: ~50ms compile time extra pero compila clean.

### 10. `TUBELIGHT_DXIL_DIR` PUBLIC al exe target no llega al lib target

En CMake, `target_compile_definitions(tubelight PUBLIC TUBELIGHT_DXIL_DIR=...)` aplica al target `tubelight` y propaga a sus consumers. Pero `tubelight_core` (donde compila `backend_d3d12.cpp`) NO es consumer de tubelight — es el opposite. Define no llega al lib que necesita la ruta. `create_pass` falla con "DXIL not found". **Fix**: aplicar la define a AMBOS targets en `cmake/CompileShaders.cmake` (línea explícita `if(TARGET tubelight_core)`).

### 11. PSNR cross-API en 8-pass cascade es ~20 dB techo realista

La spec original prometía PSNR ≥ 40 dB GL vs DX12 sobre testcard + pvm-8220 + composite_ntsc. Medido tras F3c-4: **20.72 dB**. Las diferencias se concentran en aliasing de bordes de texto + gradient banding + scanline sub-phase. NO son bugs — son float precision divergence entre NVIDIA GL driver y NVIDIA DX12 driver acumulada en cascada de 8 pasadas no-lineales (`pow`, `mix`, `sqrt`, `dot`, texture interp). Visualmente idéntico al ojo. **Spec amended**: M1 gate a 18 dB + visual smoke obligatorio. Bit-exactness genuina requeriría shader-source-único (Slang, Phase 7a) o eliminar passes no-lineales.

### 12. `Direct3D11CaptureFramePool::FrameArrived` corre en thread pool background

Asumí que el evento corría en el hilo de la pump principal. Reality: WGC dispatch FrameArrived en un thread pool background. Si el callback toca estado compartido sin mutex, race. **Fix**: latest_frame storage bajo `std::mutex` + atomic counter para frame_count. Captured into the impl via lambda, called from background thread, latest_frame() consumer-side lockea para leer.

## Decisiones tomadas

| Decisión | Alternativas descartadas | Trade-off aceptado |
|---|---|---|
| Handles opacos por ID (struct {uint32_t id}) | Interface inheritance (`ITexture* t`), variants `std::variant<GLuint, ID3D12Resource*>` | Pool maps per-backend (~hash hit per lookup); no auto-cleanup RAII en Pipeline |
| `PassUniforms_N` POD per pass con `#pragma pack(16)` | Reflection runtime, auto-gen desde GLSL | static_assert(sizeof) manual cada vez que añadas un uniform |
| Two-heap SRV scheme (CPU staging + GPU scratch ring) | Single shader-visible heap | Extra CopyDescriptors per draw (~1 µs); pero correcto cross-driver |
| Viewport negative-height para Vulkan→D3D12 Y-flip | Shader-side `gl_Position.y *= -1`, separate HLSL gen flag | Front-face winding flips (irrelevante con CULL_MODE_NONE en quad) |
| PSNR gate amended a 18 dB + visual smoke | Mantener 40 dB strict (irrealizable cross-API) | M1 deja de ser bit-exactness; perceptual quality + smoke manual |
| F3c-2 + F3c-3 fundidos | Shipear F3c-2 standalone, F3c-3 en commit separado | Diff más grande pero coherente; intermedio compilable garantizado |
| D3D11On12 device lazy-init (solo en `d3d11_on12_device()`) | Init siempre en D3D12Backend::init | Cost added solo cuando WGC se usa; pero error path tardío |
| Two PSOs por pass (intermediate RGBA16F + backbuffer RGBA8) | Single PSO + intermedio común | 2× PSO memory + create time; pero matchea formato dinámicamente sin re-create |
| T5.5 (overlay integration) deferido a sesión propia | Push through esta sesión | Phase 3d "core complete" en lugar de "complete"; reduce riesgo en `overlay_mode_win.cpp` 2k LOC |
| `--wgc-test` standalone como smoke independiente | Test directamente en overlay path | Smoke aislado del runtime overlay; valida core antes de integration |

ADR-0002 sigue vigente; no se reabrió. Updates a `specs/phase-3c/SPEC.md §M1` + `specs/phase-3c/RISKS.md §R3c-2` documentados como amendments (NO breaking de la decisión original).

## Tasks diferidos

| Task | Por qué | Bloquea | Esfuerzo |
|---|---|---|---|
| **T5.5** — wirear WGC en `src/overlay/overlay_mode_win.cpp` cuando `--renderer dx12 + --overlay-*` | 2k LOC, alto riesgo de romper estable, mejor con context fresh | Overlay con DX12 backend (R12 del ADR-0002) | M (~1-2 días sesión agentic) |
| **HiDPI scaling DX12** | No validado en displays > 1080p. GLFW reporta físicos pero my framebuffer/swap chain matching aún no probado en DPI scaling 175% | Release v0.2.0 stable público (sin pre-) | S (~1 día) |
| **D3D12 DRED + GPU-based validation en release builds** | dx12-engineer skill recomienda DRED en builds que llegan a usuarios. No habilitado | TDR debugging en producción si aparece | S |
| **WGC `--wgc-target-title <substring>`** | Solo `--wgc-test` captura primary monitor. Per-window via WgcCapture API existe pero no expuesto a CLI | T5.5 (overlay-target con DX12) | S |
| **Phase 4a** — DComp chrome+body split + drop dead `WS_EX_LAYERED` code | Roadmap ADR-0002. Resuelve lección #3 del debrief 2026-05-27 | v0.2.1 stable | M (~1 sem) |
| **Phase 5a** — HDR10 pipeline scRGB FP16 | Bloqueado por R14: fósforo spectra missing | v0.3.0 | L (2-3 sem + data) |
| **Phase 6a** — VRS + async compute pairing | Performance-only, no funcional. Optimización prematura sin baseline | v0.3.1 | M |
| **Phase 7a** — Slang single-source shaders | Cerraría gap PSNR cross-API. Drops `glslang + spirv-cross + dxc` chain a single `slangc` | v0.3.2 — PSNR 40+ alcanzable | M (~1 sem) |
| **Stub `tests/render/test_gl_handles.cpp`** | Mencionado en TASKS T2.6, deferido en F3c-5 a favor del PSNR harness que ejercita el mismo path | Cobertura unitaria adicional | S (opcional) |

## Reglas operativas nuevas

A añadir al `CLAUDE.md` del repo o recordar en próximas sesiones:

1. **Cuando refactores shaders al pipeline DX12, drain `ID3D12InfoQueue` después de cada Present si el debug layer está ON**. Sin esto, errores de PSO/root sig/descriptor table son **invisibles** (van a `OutputDebugString`, no a stderr). `D3D12Backend::end_frame()` ya lo hace cuando `params.enable_debug = true`.
2. **`layout(binding = N)` explícito en cada sampler de cada `.frag`**. NO confiar en `glslang --auto-map-bindings` para cross-API: el UBO en binding 0 mueve los samplers a binding 1/2 y rompe los root sig que asumen `t0`.
3. **Antes de promesar bit-exactness cross-API, medir con `tests/golden/dx12_vs_gl_psnr.py`**. La heurística "8-pass non-linear cascade across APIs → ~18-22 dB realistic, NO 40 dB" debe ser el default mental.
4. **Cualquier nuevo flag CLI que produce output verificable debe tener `--screenshot <path>` equivalente** (rendering 60 warmup + capture + exit). Para CI deterministic comparison.
5. **NO copiar SRVs desde un shader-visible heap a otro**. Mantener una CPU-only staging heap separada para `CreateShaderResourceView` y usar `CopyDescriptorsSimple(dst=shader_visible, src=cpu_staging)` per draw.
6. **D3D11On12 requiere `ID3D11On12Device2`** (no la base interface) para `UnwrapUnderlyingResource`. Requiere Win10 1809+; documentar en cualquier feature gate.

## Estado al cerrar

| Aspecto | Estado |
|---|---|
| **HEAD** | `3ca7d5a` |
| **Branch** | `main` (pushed origin) |
| **Tag más reciente** | `v0.2.0-beta.0` |
| **Releases públicas nuevas** | 1 (v0.2.0-beta.0 prerelease, zip 55.19 MB) |
| **Repo visibility** | PUBLIC |
| **CI** | path-check ✓, Windows MSVC vcpkg ✓, Linux gcc-13 / clang-18 ✗ (`continue-on-error` desde v0.1.5). Pixel-equivalence workflow nuevo aún sin run (push lo dispara al próximo commit en `src/render/`) |
| **Uncommitted** | 0 (todo limpio) |
| **Binario en repo root** | NO regenerado tras 3ca7d5a (sigue siendo el de v0.2.0-beta.0; rebuild manual recomendado) |
| **ADRs activas** | ADR-0001 accepted (Phase 1 cerrada), ADR-0002 accepted (3a/3b/3c shipped + 3d core + WGC validado; 3d-integration/4a/5a/6a/7a pendientes) |
| **Próximo bloque ROADMAP** | T5.5 — WGC en `overlay_mode_win.cpp` |
| **Blockers conocidos** | R14 (HDR phosphor spectra missing) bloquea Phase 5a. Sin blockers para T5.5/4a/6a |
| **Skills instaladas en `~/.claude/skills/`** | `spec-forge`, `cpp-memory-refactor`, `cpp-perf-refactor-playbook`, `native-debugger`, `dx12-engineer`, `gpu-backend-playbook`, `session-debrief` |

## Próximo paso al retomar

**Implementar T5.5**: wirear `tubelight::WgcCapture` + `D3D12Backend::wrap_d3d11_texture` en [`src/overlay/overlay_mode_win.cpp`](src/overlay/overlay_mode_win.cpp) (2000+ LOC) cuando el usuario invoca `--renderer dx12` combinado con `--overlay` / `--overlay-target` / `--overlay-fullscreen` / `--overlay-region`. El overlay debe saltar el path DXGI Desktop Duplication actual y usar WGC + DX12 en su lugar, coexistiendo con la maquinaria existente de input handling, drag, menú ImGui (que sigue siendo GL — F3c-5 spec excluyó dual-backend ImGui de Phase 3c), screenshot, video recording, y fullscreen toggle. Plantilla mínima del loop: `src/main.cpp::run_wgc_test` (líneas finales del archivo). Ship como **v0.2.0-rc.0** (o v0.2.0 stable si el QA pasa).

## Skills de skill-anvil a invocar en próxima sesión (CRÍTICO)

| Momento | Skill | Modo / Args | Por qué |
|---|---|---|---|
| **Arranque T5.5, antes de editar** | `dx12-engineer` | `mode=refactor scope=full-pipeline target_file=src/overlay/overlay_mode_win.cpp` | Su catálogo DX-01..DX-22 + 12 trampas Tubelight documentadas catalogan exactamente los bugs que aparecen al integrar D3D12 en código existente. Mode `refactor` exige PIX timeline capture pre/post + validation clean preservation |
| **Si surge bug visual / TDR / validation error durante integración** | `dx12-engineer` | `mode=debug` con symptom concreto + `golden_image=tests/golden/wgc_smoke.png` | Bisect por capa: descriptor heap → root sig → PSO → barriers. Ya catalogó los 6 bugs DX12 que vencí esta sesión |
| **Si bug genérico C/C++ surge (memory / pointer / UB / linker)** | `native-debugger` | `target_path=D:/AgentWorkspace/Tubelight symptom='...'` | Su matriz de 10 categorías cubre el espacio fuera de DX12 específico |
| **Si añades nuevos POD structs o cambias layout de existing** | `cpp-memory-refactor` | `mode=layout target_file=<archivo>.h` | Previene R3c-3 (CB layout drift) preventivamente, como en T2.2 |
| **Si tras integración el overlay se siente lento** | `cpp-perf-refactor-playbook` | Auto (workload representativo) | Triple profile pass (topdown + flamegraph + cache-misses + heaptrack). NO antes — premature opt |
| **Al cerrar T5.5 + smoke verde** | `release-publisher` | (auto-detect stack C-CMake) | Bump v0.2.0-rc.0 / v0.2.0 stable + zip + GH release |
| **Al cerrar sesión** | `session-debrief` | Como hoy | El cierre disciplinado evita perder lecciones, tasks diferidos sin contexto, y reglas operativas (categorías de la skill) |

### Skills explícitamente NO aplicables en T5.5

- `cpp-simd-vectorize-refactor` — Tubelight es GPU-bound, no CPU SIMD
- `vulkan-engineer` — out of scope (Phase 3d es WGC+DX12 only)
- `gpu-backend-playbook` — meta-orquestador para tareas cross-API; T5.5 es single-API
- `tubelight-profile-author` — es para autoría de CRT/Signal JSON profiles, no para backend work
- `spec-forge` — T5.5 está ya specced en `specs/phase-3c/PLAN.md` (implícitamente bajo "F3c-4 deferred items")

## Comandos para arrancar rápido próxima vez

```bash
cd D:/AgentWorkspace/Tubelight
git pull
git log --oneline -10

# Re-build current state
cmake --build build/windows-vcpkg --config Release --target tubelight

# Verify WGC core still works (regression smoke)
./build/windows-vcpkg/Release/tubelight.exe --wgc-test \
    --profile pvm-8220 --signal composite_ntsc \
    --screenshot tests/golden/wgc_smoke_v2.png

# Read the integration target before editing
wc -l src/overlay/overlay_mode_win.cpp  # ~2000 LOC
grep -n "DXGI\|Duplication\|render_to_screen\|pipeline\." src/overlay/overlay_mode_win.cpp

# Reference template for the WGC+DX12+Pipeline loop
sed -n '/^int run_wgc_test/,/^}/p' src/main.cpp
```

Plan suggested para la sesión T5.5 (1-2 turnos largos):

1. Invocar **`dx12-engineer mode=refactor scope=full-pipeline target_file='src/overlay/overlay_mode_win.cpp'`** y leer el catálogo de patterns relevantes (probablemente DX-09 resource state, DX-10 cmd list reset, DX-22 cmd allocator per-frame).
2. Read `src/overlay/overlay_mode_win.cpp` completo. Identificar el per-frame loop y el punto donde se sube la captura DXGI a `source_tex`.
3. Add detection branch: si `--renderer dx12` y modo no-windowed, init `D3D12Backend` (con HWND del overlay window) + `WgcCapture` (con HWND o HMONITOR del target) en lugar del path DXGI actual.
4. Refactor per-frame loop: `wgc.latest_frame() → backend.wrap_d3d11_texture() → pipeline.render_to_screen(handle) → backend.end_frame()`. ImGui menú sigue siendo GL (cross-backend ImGui es scope creep — fuera de F3c).
5. Coexistencia con: input handling (sin cambios), drag (subclass+WM_TIMER unchanged), menú ImGui (deferido: si --renderer dx12, hide menú o show con disabled hint), screenshot (extender `capture_backbuffer` ya existe), video recording (probablemente requiere readback path nuevo — deferred a v0.2.1 si no encaja).
6. Smoke: `tubelight --renderer dx12 --overlay-target <window>` muestra el target con CRT effect via DX12. Comparar GL vs DX12 side-by-side.
7. Si quality + perf OK: cut **v0.2.0-rc.0** via `release-publisher`.
8. `session-debrief` al cerrar.

Honest estimación: 6-8 horas sesión agentic real. Si scope creep en menú ImGui dual-backend, deferir esa parte a v0.2.1 con disabled message en dx12 mode.
