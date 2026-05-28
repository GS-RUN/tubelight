# Tubelight — Spec Index

> Generado siguiendo `spec-forge` v0.1.0. Estado: **LOCKED 2026-05-26** — el usuario aprobó SPEC, PLAN y CONSTITUTION; cambios posteriores requieren ADR explícito en `docs/adr/`.

| Artefacto | Propósito | Estado | Path |
|---|---|---|---|
| CONSTITUTION | Reglas inmutables | **LOCKED** | [CONSTITUTION.LOCKED.md](CONSTITUTION.LOCKED.md) |
| SPEC | Qué se construye, métricas, no-goals | **LOCKED** | [SPEC.LOCKED.md](SPEC.LOCKED.md) |
| DESIGN | Arquitectura + decisiones citadas | DRAFT | [DESIGN.md](DESIGN.md) |
| CONTRACTS | IPC schema, profile JSON schema, CLI | DRAFT | [CONTRACTS.md](CONTRACTS.md) |
| PLAN | 7 fases con gates binarios | **LOCKED** | [PLAN.LOCKED.md](PLAN.LOCKED.md) |
| TASKS | Items granulares S/M/L con DoD | DRAFT | [TASKS.md](TASKS.md) |
| TESTS | 8 suites con casos concretos | DRAFT | [TESTS.md](TESTS.md) |
| RISKS | 10 riesgos con circuit breaker | DRAFT | [RISKS.md](RISKS.md) |

**Investigación base**: [`../docs/research/SOURCES.md`](../docs/research/SOURCES.md) — fuentes citadas con URL + huecos marcados.

**Manual de usuario**: [`../docs/USER_GUIDE.md`](../docs/USER_GUIDE.md) — build + run cross-platform.

**Última actualización**: 2026-05-27 (post-v0.1.3 + ADR-0001 abierta para Phase 1 UX always-on)
**HEAD git**: `a4038ee` en `main` (público en https://github.com/GS-RUN/tubelight)

## ADRs activas

| ADR | Estado | Resumen |
|---|---|---|
| [ADR-0001](../docs/adr/0001-always-on-recordable-and-clickthrough.md) | accepted | Drop de Ctrl+Alt+R y Ctrl+Alt+C como toggles; recordable + click-through son siempre on. Chrome visible siempre + body click-through. Drag/resize via low-level mouse hook. Fix Mag `src_rect_` bug. Aplica a v0.1.4. |
**Próxima decisión pendiente del usuario**:
1. Revisar SPEC.md (especialmente métricas M1-M8 y no-goals N1-N7).
2. Aprobar/ajustar PLAN.md (orden de fases, estimaciones).
3. Decidir si activar pre-commit hook C1 desde día 1 (recomendado).
4. Decidir si la repo será pública desde F1 o privada hasta F4.

## Decisiones del usuario (2026-05-26)

- **NI-1** — Nombre: **Tubelight** ✅
- **NI-2** — Licencia: **MIT** ✅
- **NI-3** — GPU mínima: **RTX 2060 / RX 5700 / Arc A580** o inferior; el shader debe **adaptarse a cualquier GPU** vía preset auto-detect (ver M6/M6b en SPEC.md). ✅
- **NI-4** — Sin firma de código EV. Aceptamos warning Windows SmartScreen documentado en USER_GUIDE. ✅
- **NI-5** — Contribución externa de perfiles **abierta desde F3** vía Pull Request. Cada PR de perfil pasa review obligatoria de mantainers verificando que cada número físico tiene `source.url` válido (cumple C2). ✅

## Locking

Cuando un artefacto sea aprobado por el usuario, renombrar a `*.LOCKED.md` para evitar deriva. Cambios posteriores requieren ADR en `docs/adr/`.

## Estado de fases del PLAN

| Fase | Estado | Commit |
|---|---|---|
| F1 — Esqueleto cross-platform y CI | ✅ done | 52c1f10 |
| F2 — Pipeline shader puro | ✅ done | d76da9f |
| F3 — Perfiles con datos reales + validador | ✅ done | e5415a2 |
| F4 — Pass −1 señal + dithering reconstruction | ✅ done | 4dc0b95 |
| F5 — Inyección Win + LD_PRELOAD Linux GL + IPC | ✅ scaffolding | e094271 |
| F6 — Vulkan layer + DX12 + PipeWire | ✅ scaffolding | 15c3b58 |
| F7 — Pulido + perfiles extra + .slangp + packaging | ✅ done | 9856f6b |

Métricas SPEC:
- **M3** (≥6 máscaras): ✅ 6 implementadas en `pass3_mask.frag`
- **M4** (≥10 CRTProfile citados): ✅ 15 perfiles bajo `profiles/crts/`
- **M5** (7 SignalProfile): ✅ 7 perfiles bajo `profiles/signals/`
- **M8** (100% perfiles con cita): ✅ verificado por validator en cada push
- **M1/M2/M6/M7**: pendientes de verificación en hardware real (deferred a v1.0 RC)

## Próxima acción

1. Push de la repo a GitHub (`gh repo create gs-run/tubelight --public --source=.`) cuando el usuario vuelva.
2. CI run para validar que el build pasa Win+Linux.
3. F7 polish iteration: temporal Pass 5 con history-FBO, ImGui UI, PipeWire D-Bus portal, M1 latency verification con AMD FLM.
4. Demo visual: capturar Sonic Green Hill ROM frame + procesar con `--profile pvm-8220 --signal composite_ntsc` → side-by-side contra CRT-Royale (cierra R3 + R9).
