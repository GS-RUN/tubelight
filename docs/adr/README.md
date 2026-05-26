# Architecture Decision Records (ADR)

> Carpeta para registrar cualquier cambio de decisión que cruce una regla de `specs/CONSTITUTION.LOCKED.md` o modifique un artefacto `*.LOCKED.md` (SPEC, PLAN).

## Cuándo abrir un ADR

- Modificar una regla de la Constitution.
- Cambiar una métrica numérica del SPEC (M1-M8).
- Reordenar fases del PLAN.
- Cambiar la arquitectura del DESIGN.md (componentes, decisiones D1-D7).
- Romper un contrato de CONTRACTS.md (IPC, profile schema, CLI).

## Formato

Un archivo por ADR con nombre `NNNN-titulo-corto-kebab-case.md` numerado secuencialmente.

```markdown
# ADR-NNNN — Título corto

**Fecha**: YYYY-MM-DD
**Estado**: proposed | accepted | superseded by ADR-NNNN | rejected

## Contexto
Qué situación motivó la decisión.

## Decisión
Qué se decide hacer.

## Consecuencias
Qué cambia, qué se rompe, qué se gana.

## Alternativas consideradas
- A: ...
- B: ...

## Referencias
- Spec / commit / discusión que dispara el ADR.
```

## Status actual

Sin ADRs todavía. El proyecto está en fase F1 con specs lockeados sin modificaciones.
