# Tests — Tubelight

## Suite: build-and-smoke

**Propósito**: demostrar que F1 está cerrada — el repo construye y arranca.
**Setup**: CI GitHub Actions, jobs `windows-2022` y `ubuntu-24.04`.
**Casos**:
- TC-S1: `cmake -B build && cmake --build build` en Win → exit 0.
- TC-S2: idem en Linux gcc-13 → exit 0.
- TC-S3: idem en Linux clang-18 → exit 0.
- TC-S4: `./tubelight --help` en Win + Linux → exit 0, output contiene los flags de CONTRACTS.md §C4.
- TC-S5: `./tubelight --version` → muestra versión + commit hash.
- TC-S6: grep paths absolutos (`[A-Z]:\\Users|/home/[a-z]+/`) en `src/ specs/ docs/` → 0 matches.
**Pass criterion**: 6/6 verde en cada job CI.
**Tooling**: GitHub Actions matrix, `ctest`.

## Suite: shader-correctness

**Propósito**: validar el pipeline 8-pass contra golden frames de referencia.
**Setup**: `tests/golden/` con 5 imágenes input PNG (NES Mario, SNES Zelda, MD Sonic Green Hill, PS1 Silent Hill, arcade CPS2 Street Fighter) y 5 golden outputs por perfil "generic-pvm".
**Casos**:
- TC-SC1: input NES Mario + perfil "generic-pvm" → output bit-comparable contra golden (tolerancia ε≤2/255 por pixel).
- TC-SC2..TC-SC5: idem para los otros 4 inputs.
- TC-SC6: identity pipeline (todos passes desactivados) → out == in exacto.
- TC-SC7: cada pass individualmente activo → no crash + output válido (no NaN, no Inf).
- TC-SC8: perfil sin Pass −1 vs con Pass −1 sobre Sonic cascada → SSIM diff ≥ 0.05 (cambio perceptual real).
**Pass criterion**: 8/8 verde.
**Tooling**: `stb_image_write` para captura, comparación SSIM custom o `pyimagesimilarity` en script de CI.

## Suite: profile-validation

**Propósito**: garantizar que todos los perfiles versionados pasan el schema y tienen citas.
**Setup**: directorio `profiles/` con todos los `.json`.
**Casos**:
- TC-PV1: cada `profiles/crts/*.json` pasa `tubelight --validate-profile` → exit 0.
- TC-PV2: cada `profiles/signals/*.json` pasa validación → exit 0.
- TC-PV3: cada bloque con números físicos (tube, phosphor, bandwidth) tiene `source.url` no vacío y `source.retrieved_at` formato fecha.
- TC-PV4: JSON manipulado para borrar `source` → exit 3 (E_PROFILE_INVALID).
- TC-PV5: JSON con `dot_pitch_mm: -1` → exit 3.
**Pass criterion**: 5/5 + 100% de los perfiles existentes pasan.
**Tooling**: ajv (Node) o `nlohmann::json_schema` en runner C++.

## Suite: signal-pipeline

**Propósito**: validar que Pass −1 transforma la señal de forma reconocible por humanos y métricas.
**Setup**: input `Sonic_GreenHill_waterfall.png` extraído de Mega Drive sin filtros.
**Casos**:
- TC-SG1: pasada con `signal=rgb_vga` (sin BW limit) → output ≈ input (SSIM ≥ 0.98).
- TC-SG2: pasada con `signal=composite_ntsc` → cascada visualmente fundida en agua (revisión humana con captura subida a `tests/visual_review/`).
- TC-SG3: pasada con `signal=rf` → ruido por línea visible + ghosting + chroma muy degradado (SSIM contra input ≤ 0.85).
- TC-SG4: dithering reconstruction activa en composite → cascada Sonic muestra agua continua; sin reconstruction → rayas visibles.
- TC-SG5: detección de dithering: input con tablero A/B uniforme → mask ≥50% true; input fotográfico → mask ≤5% true.
**Pass criterion**: 5/5 + revisión humana firma TC-SG2 con OK visual.
**Tooling**: SSIM + revisión manual checklist.

## Suite: latency

**Propósito**: medir M1 (camino feliz) y M2 (fallback).
**Setup**: máquina con AMD FLM instalado, monitor 60 Hz, vídeo de referencia `tests/latency_reference.mp4` (clip con clap visual).
**Casos**:
- TC-L1: target = RetroArch DX11, hook activo → AMD FLM reporta latency_added < 2 ms (mediana de 100 mediciones).
- TC-L2: target = mednafen GL Linux, LD_PRELOAD activo → herramienta equivalente Linux (`glx_hook` perfil) reporta < 2 ms.
- TC-L3: target = RetroArch DX11, fallback DXGI activo → 16.7 ms ≤ added ≤ 33.3 ms (1-2 frames).
- TC-L4: target = juego Vulkan (RPCS3 o Dolphin), Vulkan layer activa → < 2 ms.
**Pass criterion**: 4/4 con mediana dentro de bounds.
**Tooling**: AMD FLM (Win), instrumentación interna con timestamps GPU (Linux).

## Suite: stability

**Propósito**: el hook no crashea el target en sesión larga.
**Setup**: RetroArch + ROM NES, sesión 1 h con cambios de perfil cada 30 s.
**Casos**:
- TC-ST1: 1 h sin crash del target.
- TC-ST2: 1 h sin crash del backend.
- TC-ST3: 100 cambios de perfil en caliente sin glitch visual permanente.
- TC-ST4: `detach` limpio: target sigue corriendo sin Tubelight visible.
- TC-ST5: kill brutal del UI process → backend hace cleanup y deshookea sin afectar target.
- TC-ST6: target cierra mientras Tubelight attached → backend recibe `detached` event sin crash de UI.
**Pass criterion**: 6/6 verde, log de cada sesión adjunto.
**Tooling**: script Python que orquesta target + UI + observa exit codes y logs.

## Suite: cross-platform-parity

**Propósito**: M7 — mismo perfil produce mismo output en Win y Linux.
**Setup**: misma GPU (RX 5700 XT) con drivers actualizados, mismo SDR display.
**Casos**:
- TC-CP1: 5 inputs × 3 perfiles → 15 outputs cada plataforma. ε≤2/255 por canal en 99% de pixels.
- TC-CP2: hash MD5 del SPIR-V compilado idéntico en ambas plataformas (descarta diferencias de glslang).
- TC-CP3: archivo `.slangp` exportado en Win se carga en RetroArch Linux sin reescritura de paths.
**Pass criterion**: 3/3.
**Tooling**: script Python con comparación numpy.

## Suite: perf

**Propósito**: M6 — 60 fps a 4K.
**Setup**: GPU referencia (RTX 2060 / RX 5700 / Arc A580). Input = vídeo 4K 60 fps.
**Casos**:
- TC-P1: pipeline completo a 4K → fps medio ≥ 60 sobre 10 s de captura.
- TC-P2: pipeline completo a 1080p → fps medio ≥ 144 (target streamers).
- TC-P3: GPU time del pipeline < 5 ms a 4K (resto del budget = 11.7 ms para target).
**Pass criterion**: 3/3 con tolerancia ε=2 fps.
**Tooling**: query GPU timestamps + script de captura.

## Suite: regression-mooneye-style (opcional, v2+)

**Propósito**: comparación contra hardware real con captura.
**Setup**: PVM-20M2 conectado a Mega Drive real + capturadora HDMI 4K que filme el tubo a 240 fps. Tubelight con mismo input + perfil "pvm-20m2-composite-ntsc" sobre captura RGB del MD.
**Casos**:
- TC-R1: SSIM Tubelight output vs captura PVM filmada ≥ 0.85.
**Pass criterion**: 1/1 (opcional para v1, mandatorio para v2).
**Tooling**: capturadora + script SSIM + alineamiento de frames.
