# Constitution — Tubelight

> Reglas que el proyecto NO cruza. Cambiarlas requiere ADR explícito en `docs/adr/`.

## C1 — Cero paths absolutos personales en repo
**Regla**: Ningún archivo versionado contiene `C:\Users\nunez`, `D:\AgentWorkspace`, ni paths concretos de la máquina de desarrollo. Todo lo configurable va por variables de entorno, CMake presets relativos, o flags CLI.
**Justificación**: la repo debe ser clonable y compilable por cualquier desarrollador o agente sin parchear paths.
**Violación detectada por**: pre-commit grep `[A-Z]:\\Users\|/home/[a-z]+/` sobre archivos staged.

## C2 — Cada dato técnico con cita
**Regla**: todo número físico (decay de fósforo, dot pitch, bandwidth, PAR) referencia su fuente con URL o documento. Si no hay fuente, se marca `[UNCITED]` o `[NEEDS-MEASUREMENT]`. No se inventan valores.
**Justificación**: el diferencial de Tubelight frente a shaders existentes es rigor físico — sin cita, es estética.
**Violación detectada por**: review manual + lint sobre archivos `profiles/*.json` y `docs/research/*.md` exigiendo campo `source`.

## C3 — Windows primero, Linux compatible desde día 1
**Regla**: el código portable (core, shaders, UI, profiles) compila en ambas plataformas desde el primer commit que lo introduce. Solo los módulos de plataforma (`injection-win/`, `preload-linux/`) son específicos. No se admite "ya lo portamos al final".
**Justificación**: bolt-on de Linux al final fuerza refactor cross-layer; mantenerlo desde el día 1 es más barato.
**Violación detectada por**: CI con jobs Windows + Linux desde F1.

## C4 — Stack abierto y portable
**Regla**: dependencias core obligatoriamente open-source con licencia permisiva o LGPL (GLFW, glslang, SPIRV-Cross, MinHook, Dear ImGui o equivalente). Nada propietario en el camino crítico.
**Justificación**: el proyecto debe ser distribuible sin licencias corporativas; cualquiera debe poder forkear y recompilar.
**Violación detectada por**: `cmake --target check-licenses` enumera deps y falla si una está fuera de allowlist.

## C5 — Latencia objetivo y degradación honesta
**Regla**: en el camino feliz (hook Present()/swap) la latencia añadida es `<2 ms` medida con FLM o equivalente. Si la inyección falla, se cae a captura DXGI/PipeWire avisando al usuario en UI: "modo captura — +1 frame de latencia".
**Justificación**: vender "cero lag" cuando hay 16.7 ms ocultos es deshonesto.
**Violación detectada por**: suite `tests/latency/` ejecutada en CI con vídeo de referencia 60 Hz.

## C6 — GLSL como source único
**Regla**: shaders se escriben una sola vez en GLSL (Vulkan dialect). HLSL para D3D y SPIR-V para Vulkan se generan vía glslang + SPIRV-Cross en build. No hay dos versiones del mismo shader que mantener.
**Justificación**: duplicar shaders es donde mueren los proyectos cross-platform.
**Violación detectada por**: ausencia de archivos `*.hlsl` o `*.spv` versionados (sólo `*.glsl`).

## C7 — Documentación viva del manual de usuario
**Regla**: `docs/USER_GUIDE.md` se actualiza en el mismo commit que cambia dependencias, plataformas soportadas o forma de arrancar la app. Sin excepción.
**Justificación**: usuario que clona la repo a las 6 meses debe poder build+run sin preguntar.
**Violación detectada por**: pre-merge check: si `CMakeLists.txt` o `vcpkg.json` cambian sin tocar `docs/USER_GUIDE.md`, warning bloqueante.

## C8 — Pre-fase gates, no "casi listo"
**Regla**: una fase del PLAN no se cierra hasta que su gate (criterio binario) pasa. No hay "fase F3 al 80%". O verde, o sigue abierta.
**Justificación**: 80% sostenido es deuda invisible que sale en F5.
**Violación detectada por**: TASKS.md no marca fase F_n como completed si su gate no está verde.
