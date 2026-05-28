# Constitution — Phase 3c (Tubelight)

> Reglas que el sub-proyecto Phase 3c NO cruza. Heredan las C1–C8 de
> [`../CONSTITUTION.LOCKED.md`](../CONSTITUTION.LOCKED.md). Cambiarlas
> requiere ADR explícito en `docs/adr/`.

## C3c-1 — GLSL sigue siendo source único (refuerza C6)
**Regla**: ningún `.hlsl`, `.dxbc`, `.dxil`, `.spv` se versiona en el repo.
HLSL se genera en build vía `glslang → SPIR-V → SPIRV-Cross → HLSL`,
DXIL via `dxc -T ps_6_0`. Los `.frag` ya existentes en `shaders/` son la
única fuente editable.
**Justificación**: C6 lo exige. Mantener dos versiones del mismo shader
es donde mueren los proyectos cross-backend.
**Violación detectada por**: pre-commit `git ls-files | grep -E '\.(hlsl|dxbc|dxil|spv)$'` debe estar vacío.

## C3c-2 — Pixel-equivalence gate cuantitativo, no "se parece"
**Regla**: cierre de Phase 3c exige PSNR ≥ 40 dB y diferencia per-canal
máxima ≤ 4/255 sobre el testcard procesado con `pvm-8220 + composite_ntsc`
entre GL y D3D12. Sin esto, no se merge.
**Justificación**: la lección #2 del debrief 2026-05-27 (merge net loss
descubierto a destiempo) aplica también a "esto se ve igual". Tolerancia
4/255 absorbe redondeo float entre drivers; PSNR 40 dB es el umbral
estándar de "visualmente indistinguible".
**Violación detectada por**: `tests/golden/dx12_vs_gl_psnr.py` en CI
local antes de cualquier tag v0.2.0-beta+.

## C3c-3 — Pipeline NO se duplica
**Regla**: queda PROHIBIDO crear `PipelineGL` y `PipelineD3D12` como
clases separadas. La orquestación de 8 pasadas (cuál samplea de cuál,
qué uniforms, snapshot de history, skip de pass 5) vive en un solo
`Pipeline` que delega recursos al backend via handles.
**Justificación**: la decisión D2 del DESIGN existente marca el
orquestador como CORE — partirlo en dos duplicaría toda la lógica de
profile binding + flags + history. Drift garantizado a los 3 meses.
**Violación detectada por**: review manual. No existir `PipelineD3D12`
en el árbol de archivos.

## C3c-4 — Phase 3c NO toca overlay
**Regla**: el camino DX12 solo se expone vía `--shader-only --renderer dx12`.
Overlay (`--overlay`, `--overlay-target`, `--overlay-fullscreen`,
`--overlay-region`) sigue OpenGL-only en esta fase. Captura WGC +
DirectComposition + click-through son Phases 3d y 4a.
**Justificación**: el alcance ya es grande (8 ports + Pipeline rewrite +
gate). Meter WGC + DComp duplica complejidad y arriesga romper el flujo
estable de v0.1.7. Lección #4 del debrief (setters públicos desde día 1)
NO ayuda aquí — esto es contención de scope, no API design.
**Violación detectada por**: `overlay_mode_win.cpp` no contiene strings
`D3D12`, `dx12`, `IDXGISwapChain` post-merge.

## C3c-5 — Sin regresión funcional en GL
**Regla**: el output de `--renderer gl --shader-only testcard.png` post-
Phase 3c es **pixel-exacto** al output de v0.1.7. Mismo binario, mismo
shader source, misma textura de salida.
**Justificación**: el refactor de Pipeline para abstraer recursos no
puede cambiar el orden de operaciones GL ni el muestreo. Cualquier diff
visible es bug, no feature.
**Violación detectada por**: golden PNG `tests/golden/gl_v017_baseline.png`
diff contra output actual (hash SHA256 o ε = 0).
