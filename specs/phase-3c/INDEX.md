# Phase 3c — Spec Index (Tubelight)

> Generado siguiendo `spec-forge` v0.1.0. Sub-spec de
> [`../INDEX.md`](../INDEX.md), aterriza Phase 3c de
> [`../../docs/adr/0002-d3d12-dcomp-wgc-hdr-slang-blueprint.md`](../../docs/adr/0002-d3d12-dcomp-wgc-hdr-slang-blueprint.md).
>
> Alcance: refactor de `Pipeline` a handles + port automático
> GLSL→DXIL + ejecución D3D12 + pixel-equivalence gate. Cierra con
> v0.2.0-beta.

| Artefacto | Propósito | Estado | Path |
|---|---|---|---|
| CONSTITUTION | 5 reglas inmutables Phase 3c (hereda C1–C8) | DRAFT | [CONSTITUTION.md](CONSTITUTION.md) |
| SPEC | Qué construye, 6 métricas, 7 no-objetivos | DRAFT | [SPEC.md](SPEC.md) |
| DESIGN | Arquitectura + 5 decisiones citadas | DRAFT | [DESIGN.md](DESIGN.md) |
| CONTRACTS | IRenderBackend v2, handles, PassUniforms, layout DXIL | DRAFT | [CONTRACTS.md](CONTRACTS.md) |
| PLAN | 5 fases F3c-1..F3c-5, ~5-7 días | DRAFT | [PLAN.md](PLAN.md) |
| TASKS | 28 items S/M/L con DoD binario | DRAFT | [TASKS.md](TASKS.md) |
| TESTS | 7 suites — build determinism, GL byte-exact, PSNR DX12 vs GL, fallback | DRAFT | [TESTS.md](TESTS.md) |
| RISKS | 9 riesgos con circuit breaker | DRAFT | [RISKS.md](RISKS.md) |

**HEAD git**: `47768d3` en `main` (v0.2.0-alpha.0).
**Última actualización**: 2026-05-28.
**Estado de partida**: D3D12Backend boota, hace clear+present, pero
`supports_pipeline()==false`. Pipeline usa tipos GL concretos
(`FBO`, `ShaderProgram`, `Texture2D`) directamente.

## Decisiones críticas resueltas en este spec

| ID | Decisión | Donde |
|---|---|---|
| D3c-1 | Handles opacos por ID (bgfx pattern) | DESIGN §D3c-1 |
| D3c-2 | Uniforms via struct POD `PassUniforms_N` | DESIGN §D3c-2 |
| D3c-3 | Backbuffer RGBA8 + intermedios RGBA16F | DESIGN §D3c-3 |
| D3c-4 | DXIL precompilado en zip, sin DLLs DXC | DESIGN §D3c-4 |
| D3c-5 | PSNR harness Python (Pillow + numpy) | DESIGN §D3c-5 |

## No-decisiones (diferidas explícitamente)

| Tema | Diferido a | Por qué |
|---|---|---|
| Slang single-source shaders | Phase 7a | C6 vigente, GLSL → SPIRV-Cross → DXIL es path estándar |
| HDR pipeline scRGB FP16 | Phase 5a | Bloqueado por R14 (fósforo spectra) |
| WGC capture + D3D11On12 | Phase 3d | Scope contention (ver C3c-4) |
| DComp chrome / body split | Phase 4a | Idem |
| VRS + async compute | Phase 6a | N1 no-goal; tentación scope creep en R3c-8 |

## Gate de cierre Phase 3c (binario, sin "casi")

- [ ] M1 verde: PSNR ≥ 40 dB GL vs DX12 sobre testcard pvm-8220+composite_ntsc.
- [ ] M2 verde: hash SHA256 GL post-3c == baseline v0.1.7.
- [ ] M3 verde: hash DXIL determinista 3 builds clean.
- [ ] M5 verde: las 8 pasadas existentes ejecutan en DX12.
- [ ] M6 verde: pr-reviewer sin P0/P1.
- [ ] CI workflow `pixel_equivalence.yml` verde primer run.
- [ ] Tag v0.2.0-beta empujado.

## Próxima acción

1. **Usuario revisa el spec** y aprueba o pide ajustes.
2. Si OK, marcar SPEC + PLAN + CONSTITUTION como `*.LOCKED.md`.
3. Empezar F3c-1 (build pipeline GLSL → DXIL). Estimado 0.5-1 día.

## Locking

Cuando un artefacto sea aprobado, renombrar a `*.LOCKED.md`. Cambios
posteriores requieren ADR explícito (ej.: `docs/adr/0003-phase-3c-amend.md`).
