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

**Última actualización**: 2026-05-26
**HEAD git**: pendiente (proyecto no inicializado todavía como repo)
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

## Próxima acción recomendada

1. Lee SPEC.md y DESIGN.md (los dos más críticos).
2. Si las métricas y no-goals te encajan, decimos "lock SPEC" y empezamos F1.
3. Si quieres ajustes, los aplicamos antes de que ningún archivo de `src/` se escriba.
