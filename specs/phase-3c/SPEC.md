# Spec — Phase 3c (Tubelight)

## TLDR (4 líneas)
Refactor de `Pipeline` para abstraer FBO/Texture/Shader detrás de
handles del backend. Port automático de los 8 fragment shaders GLSL → DXIL
vía `glslang + SPIRV-Cross + dxc` en build. `D3D12Backend::supports_pipeline()`
pasa a `true` y el comando `--renderer dx12 --shader-only` produce el
mismo output visual que `--renderer gl`. Cierra v0.2.0-beta.

## Problema

HEAD `47768d3` (v0.2.0-alpha.0) tiene el backend D3D12 booteando, pero
la pipeline de 8 pasadas sigue siendo GL-only. Causa raíz:
`src/core/pipeline.h:175-188` define los recursos como tipos GL
concretos:

```cpp
std::array<ShaderProgram, kPassCount> shaders_;   // GLuint program
std::array<FBO, kPassCount>           fbos_;      // GLuint fbo + texture
FBO  history_fbo_;
Texture2D bezel_image_;
```

Y `Pipeline::render_to_screen` ([pipeline.cpp:237-334](src/core/pipeline.cpp:237))
llama directamente a `sh.use()`, `sh.set_*()`, `glActiveTexture`,
`glBindTexture`, `glCopyTexSubImage2D` — todo GL puro. Para que DX12
ejecute la pipeline tenemos que (a) abstraer estos tipos detrás del
backend y (b) tener los shaders compilados a DXIL.

Sin esto, ADR-0002 Phase 3d (WGC capture) y 3e (bench publicado) están
bloqueadas — no hay nada que medir y nada donde meter la textura WGC.

## Usuarios y casos de uso

- **Desarrollador (yo / colaborador futuro)**: ejecuta
  `tubelight --renderer dx12 --shader-only testcard.png --profile pvm-8220 --signal composite_ntsc`
  y obtiene el mismo CRT-look que el GL path. Frecuencia: cada commit
  que toca render path (~ 10–20 veces hasta v0.3.x).
- **CI agentic**: corre el test suite de pixel-equivalence en cada
  push a `main` con commits que tocan `src/render/` o `shaders/`.
- **Usuario final post-v0.2.0**: nota framerate ~3-5× mejor cuando elige
  `--renderer dx12` sobre un juego DirectX (Phase 3e mide y publica).
  En v0.2.0-beta NO se le promete nada: el flag existe, el output es
  correcto, las métricas son problema de 3e.

## Objetivos medibles

- **M1 — Pixel equivalence GL vs D3D12**: PSNR ≥ **40 dB** sobre el
  testcard (`docs/manual/assets/raw/testcard.png`, 1280×960) procesado
  con `pvm-8220 + composite_ntsc`. Diferencia per-canal máxima ≤ **4/255**.
  Medido por `tests/golden/dx12_vs_gl_psnr.py`.
- **M2 — No regresión GL**: hash SHA256 del output GL post-3c == hash
  del output GL en v0.1.7 baseline (capturado en
  `tests/golden/gl_v017_baseline.png`). Es decir, **byte-exacto**.
- **M3 — Shader build determinista**: el comando `cmake --build` genera
  los 8 DXIL bytecodes a partir de los `.frag` sin cambios manuales, y
  el hash del DXIL es estable a lo largo de 3 builds consecutivos en la
  misma máquina (verifica `dxc` determinismo).
- **M4 — Tiempo de port por pass**: el flujo
  `(edit .frag) → (cmake --build) → (smoke en DX12)` cierra en ≤ **30 s**
  por iteración en hardware referencia (RTX 2080 Ti, MSVC Release).
  Medido informalmente al portar la pasada más compleja
  (`pass6_composition.frag`, 329 LOC).
- **M5 — Cero shaders sin port**: las 8 pasadas (Pass −1, 0, 1, 2, 3, 4,
  5, 6) tienen su `.dxil` generado y son ejecutadas en el camino DX12.
  Si una sola falla, M1 no aplica.
- **M6 — Code review verde**: `pr-reviewer` 7/7 categorías sin P0 ni
  P1 sobre el diff de Phase 3c.

## No-objetivos (explícitos)

- **N1 — Performance**: NO se mide ni se publica FPS / CPU% en 3c.
  Phase 3e existe exactamente para eso. La implementación 3c puede ser
  conservadora (wait-for-idle per frame, single command list) — el
  pipelining triple-buffer es 3e.
- **N2 — Slang migration**: GLSL es source-of-truth. Slang es Phase 7a
  (ADR-0002 §Plan). Adelantarlo aquí es scope creep.
- **N3 — WGC capture**: la textura de entrada se sigue cargando vía
  `Texture2D::load_from_file()` (stb_image → CPU buffer → upload). DX12
  hace su propio upload path. WGC + D3D11On12 es Phase 3d.
- **N4 — DirectComposition / chrome separation**: el window chrome y
  click-through siguen como están en v0.1.7. Eso es Phase 4a.
- **N5 — HDR pipeline**: backbuffer en `R8G8B8A8_UNORM` 8-bit sRGB. FP16
  end-to-end es Phase 5a y depende de R14 (fósforo spectra).
- **N6 — Overlay en DX12**: ver C3c-4. El flag funciona solo con
  `--shader-only`. Overlay queda GL hasta Phase 3d cierre.
- **N7 — Hooks de juego (DX12 inject)**: el `injection-win/` actual
  ataca el swap chain del juego, no Tubelight. No relacionado con esta
  fase.

## Glosario

- **DXIL**: DirectX Intermediate Language. Bytecode shader que consume
  D3D12 via `D3D12_SHADER_BYTECODE`.
- **SPIRV-Cross**: librería Khronos que transpila SPIR-V a HLSL/MSL/GLSL.
- **`glslang`**: el frontend de referencia GLSL → SPIR-V.
- **`dxc`**: compilador HLSL → DXIL (sucesor de `fxc`).
- **Root signature**: el equivalente D3D12 a un layout de descriptor
  sets Vulkan / uniforms GL. Declara qué recursos lee el shader.
- **PSO**: Pipeline State Object. Inmutable, agrupa shaders + root sig
  + estados fijos (blend, raster, depth). Una por pasada.
- **Pixel equivalence**: PSNR ≥ 40 dB + Δ per-canal ≤ 4/255 entre dos
  imágenes RGBA. No es bit-exactness — los drivers difieren en
  redondeo float.
