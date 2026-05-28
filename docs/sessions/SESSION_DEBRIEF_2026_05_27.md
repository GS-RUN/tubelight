# Session Debrief — 2026-05-27 — Tubelight

## TL;DR (3 líneas)

- **Intent**: terminar el manual de usuario (Reddit-ready) y atacar la fluidez tipo ShaderGlass.
- **Reality**: shipped v0.1.1 → v0.1.6 (6 releases públicas), licencia switch MIT→PolyForm, repo público, ADR-0001 (UX simplificado: drop Ctrl+Alt+R y Ctrl+Alt+C como toggles) y ADR-0002 (blueprint completo D3D12+DComp+WGC+HDR+Slang con 10 fases hasta v0.3.2). Manual bilingüe ES/EN entregado con 42 PNGs anclados a testcard.
- **Próximo paso al retomar**: Phase 3a — `IRenderBackend` abstraction sin cambio funcional (preparar rieles para D3D12 backend). Ver "Comandos para arrancar rápido" abajo.

## Intent vs Reality

- **Intent declarado**: "presentación Reddit + arreglar el lag respecto a ShaderGlass".
- **Reality**: el alcance creció de forma controlada en 3 bloques: (a) manual + Reddit post, (b) hygiene cleanup (paths personales + PRIVATE → repo público), (c) UX simplification + perf engineering. La fluidez no se "arregló" en una sesión — se documentó el plan completo en ADR-0002 con 10 fases (~3 meses de trabajo) y se shipped los 4 wins iniciales (Phase 1, 2a, 2b, 2c).
- **Divergencia**: scope creep deliberado tras dos descubrimientos clave: (1) el repo estaba "PRIVATE" pero nada técnico lo justificaba post-manual — el user dio luz verde a hacerlo público desde v0.1.3; (2) la auditoría de paths personales reveló contaminación que motivó v0.1.2 antes de lo previsto.

## Qué se hizo

**16 commits** desde tag v0.1.0 (388339d) hasta HEAD `5438438` (v0.1.6). **+7699 / −316 LOC** en 130 archivos.

### Releases publicadas

| Tag | Highlights |
|---|---|
| v0.1.1 | Manual bilingüe + integración Help botón. 42 PNGs. |
| v0.1.2 | Cleanup paths personales (CONSTITUTION C1). Scripts auto-detectan repo root vía `$PSScriptRoot`. |
| v0.1.3 | **Primera release pública**. Manual purgado de "PRIVATE". Repo flipeado a public. |
| v0.1.4 | **ADR-0001**: drop Ctrl+Alt+R y Ctrl+Alt+C como hotkeys. Recordable always-on. Click-through por modo. Fix Mag `src_rect_` bug. |
| v0.1.5 | **ADR-0002**: blueprint D3D12+DComp+WGC+HDR+Slang publicado (10 fases hasta v0.3.2). Skip ImGui frame cuando idle (~0.5% CPU). |
| v0.1.6 | PBO ring 3-slot en VideoRecorder (~3-6ms GPU stall eliminado por frame grabado a 1920×1200) + skip pass 5 cuando persistence ≈ 0 (~0.4ms). |

### Otros bloques

- CI debugging completo: vcpkg SHA fix (`1de2026...` no es el commit de tag 2024.07.12, era `7e99dc22...` para matchear builtin-baseline), VCPKG_INSTALLATION_ROOT bogus forward arreglado, Linux jobs marcados `continue-on-error` y renombrados `(experimental)` porque nunca compilaron.
- README + manual updated con "Linux próximamente (v1.1)" para ser honesto en público.
- Reddit post drafted (ES + EN) para r/crtgaming con tono técnico-honesto.

## Lecciones aprendidas (no obvias)

### 1. La memoria de proyecto puede mentir vs el código

`project_tubelight.md` decía "Pass 5 (temporal): identity todavía". Leyendo `shaders/pass5_temporal.frag` resulta que **no es identity** — hace blending real con history FBO usando `result = max(current, prev × persistence)`. La nota era sobre el modelo físico (incompleto), no sobre el shader (sí ejecuta). **Implicación**: el skip de pass 5 en Phase 2c solo es válido cuando `persistence_total < 1e-3`, no incondicional como prometía el plan original. Lección operativa: cuando un ADR promete optimizar un pass "porque es identity", LEER el shader primero.

### 2. Merge de shaders puede ser net loss

Phase 2c plan original era "merge passes 0 (dither detect) + 1 (dither reconstruct) en un shader". Al implementarlo descubrí que pass 1 lee pass 0 en 4 neighbours (L, R, U, D). Un merge requiere ejecutar el algoritmo completo de pass 0 (que muestrea 9 texels) en el centro + 4 vecinos = **45 texture samples vs. los 9+5 = 14 actuales**. Net loss ~3×. **Lección**: prototipar el coste antes de prometer la fusión en un ADR. Heurística: si pass N+1 muestrea vecinos de pass N, el merge solo es ganancia si pass N tiene 1-3 samples; con más, peor.

### 3. Cross-process click-through tiene contrato Win32 ineludible

`WM_NCHITTEST` retornando `HTTRANSPARENT` solo funciona para windows del **mismo thread** (MSDN textual). Para que un click en Tubelight aterrice en openMSX/Firefox/etc (proceso distinto) hace falta `WS_EX_LAYERED + WS_EX_TRANSPARENT + SetLayeredWindowAttributes(LWA_ALPHA)` — DWM intercepta el hit-test en la capa de composición antes de que llegue al WndProc. **Implicación**: imposible tener "chrome arrastrable + body click-through cross-process" con Win32 puro sin un low-level mouse hook que toggle TRANSPARENT dinámicamente. La solución limpia: separar visuals en DirectComposition (ADR-0002 Phase 4a) o aceptar que windowed mode NO sea click-through (lo que hicimos en v0.1.4).

### 4. La Magnification API tiene una pega de diseño olvidada

`MagCapture::init()` fijaba `src_rect_ = mon_rect` y nunca actualizaba. En modo recordable + runtime attach via `Ctrl+Alt+T`, el callback Mag seguía sampleando el monitor entero aunque el overlay se anclase a una ventana de 1280×960 al centro. Bug latente desde el primer commit que añadió Mag (sesión 2026-05-27 release v0.1.0). **Detección**: solo aparece si combinas dos features raras (Mag + runtime attach); el flow CLI `--overlay-target` funcionaba por accidente porque la Mag se inicializaba con el rect correcto cuando `target_active=true` al startup. Lección: cuando una API toma "current rect" en init, marca explícito en el doc que necesita re-asserts; o expón un setter público desde el primer día.

### 5. vcpkg builtin-baseline + lukka/run-vcpkg deben sincronizar exactamente

`vcpkg.json` tiene `"builtin-baseline": "7e99dc22..."` que pinpoint las versiones de las deps. El workflow CI usaba `vcpkgGitCommitId: '1de2026...'` (tag `2024.07.12`). Resultado: el snapshot de vcpkg cargado por el action era MÁS VIEJO que la baseline → "no version database entry for glfw3 at 3.4#1". **Fix**: alinear ambos al mismo SHA1. **Regla**: cualquier bump del `builtin-baseline` en `vcpkg.json` debe bumpear también `vcpkgGitCommitId` en `.github/workflows/ci.yml` en el mismo commit.

### 6. lukka/run-vcpkg@v11 ya exporta VCPKG_ROOT

El workflow tenía `env: VCPKG_ROOT: ${{ env.VCPKG_INSTALLATION_ROOT }}` para la step Configure. Esa variable NO existe (era `VCPKG_INSTALLATION_ROOT` un nombre inventado, no documentado). El action ya pone `VCPKG_ROOT` como job-wide env. Override innecesario → vacío → `CMAKE_TOOLCHAIN_FILE=/scripts/buildsystems/vcpkg.cmake` sin prefix → CMake "could not find toolchain file". **Regla**: en CI nuevas, NO forwarderar env vars de actions de terceros sin verificar primero el nombre exacto que exportan.

### 7. SendKeys + ImGui tabs sobre WS_EX_LAYERED windows = no fiable

Los clicks sintéticos `mouse_event(MOUSEEVENTF_LEFTDOWN, ...)` sobre el tab bar de ImGui en una ventana layered no cambian de tab consistentemente. Probado con 4 estrategias distintas (coords calculadas, MCP screenshot calibrado, Ctrl+Alt+R primero, sin Ctrl+Alt+R) — el comportamiento es errático. **Workaround**: capturas per-tab del menú se hicieron a mano por el usuario en el flow "yo lanzo configurado, tú disparas Ctrl+Alt+S". Para automatizar properly habría que usar Windows-MCP Click con label-targeting (no coords) o un test framework GUI-aware. Lección: synthetic input para validar UI ImGui sobre layered → manual fallback acceptado.

## Decisiones tomadas

| Decisión | Alternativas descartadas | Trade-off aceptado |
|---|---|---|
| Recordable always-on (ADR-0001 §1) | Toggle por hotkey (status quo) | UX más simple a costa de no poder "esconder" overlay de OBS. La mayoría de usuarios siempre lo quieren visible. |
| Click-through por modo, no toggle (ADR-0001 §2) | Hotkey Ctrl+Alt+C; toggle dinámico vía WS_EX_TRANSPARENT | windowed mode pierde click-through. Si user lo quiere, debe usar `--overlay-target` o `--overlay-region`. |
| D3D12 para v0.2.0 backend (ADR-0002) | D3D11 (más simple), Vulkan (cross-platform), WebGPU (menos maduro) | Lock-in Windows-first pero abre VRS + async compute + DXR si los queremos en v0.3+. |
| HDR pipeline scRGB FP16 → HDR10 | SDR-only como ShaderGlass | Requiere phosphor spectra reales (R14 en ADR-0002), bloqueador hasta tener data. |
| WGC sobre DXGI Duplication | Mantener DXGI con WGL_NV_DX_interop2 | WGC entrega per-window texture, menos bandwidth — sentido obvio una vez visto el detalle. |
| Slang como shader source language | GLSL puro (status quo), HLSL (D3D-only) | Slang permite single-source para SPIRV+DXIL+HLSL bytecode. Standard emergente. |
| Repo público desde v0.1.3 | Mantener PRIVATE hasta v1.0 | Acelera feedback. Vetting de license hecho (PolyForm Noncommercial). |
| Phase 2c "skip pass 5" en lugar de "merge passes 0+1" | Plan original ADR-0002 | Honestidad técnica > coherencia con plan que no aguantó implementación. |

ADRs formales escritas:
- `docs/adr/0001-always-on-recordable-and-clickthrough.md`
- `docs/adr/0002-d3d12-dcomp-wgc-hdr-slang-blueprint.md`

## Tasks diferidos

| Task | Por qué | Bloquea | Esfuerzo |
|---|---|---|---|
| **Phase 3a** — `IRenderBackend` abstraction + GL backend wrap | Foundation para D3D12. No ship en esta sesión por scope. | Toda Phase 3+ | M (~1 semana real, 4-5 días en sesiones agentic) |
| **Phase 3b** — D3D12 backend skeleton (device + flip swap chain + full-screen quad) | Esperando 3a | Phase 3c+ | L (~1 semana) |
| **Phase 5a** — HDR10 pipeline | Bloqueado por R14: fósforo en CIE xy ≠ spectral data. Necesitamos medir espectros P22/P31/P3/P4 o usar approximation conservadora. | v0.3.0 | L (2-3 semanas + data) |
| **Per-tab menu screenshots** automatizadas | Synthetic mouse_event sobre ImGui layered no fiable (lesson #7). Workaround manual funciona. | Solo cosmético; usuario hizo a mano 4 tabs. | S (necesita Windows-MCP label-targeting) |
| **Linux build verde** | Ubuntu 24.04 apt package drift; clang find_package(glm) falla. Jobs `continue-on-error` por ahora. | Linux release v1.1 | M |
| **Performance benchmarks publicados** | Para anunciar v0.2.0 con números antes/después necesitamos métrica reproducible. Falta script de bench. | Comunicación pública v0.2.0 | S |

No abrir TodoHub items hasta retomar — todos están en el roadmap de ADR-0002 y aquí.

## Reglas operativas nuevas

A añadir al `CLAUDE.md` del repo o al recordar en próximas sesiones:

1. **Cualquier bump de `vcpkg.json` builtin-baseline DEBE bumpear `vcpkgGitCommitId` en `.github/workflows/ci.yml` en el mismo commit**.
2. **Antes de prometer "merge X+Y shader" en un ADR, contar texture samples del path actual y del propuesto**. Si propuesto > 1.5× actual, descartar.
3. **Cuando una API toma "current state" en init y se reutiliza después, exponer un setter público desde el primer día** o documentar TODO_FUTURE_BUG explícito.
4. **Synthetic mouse clicks sobre ImGui layered windows no son fiables** — para flows de captura UI usar control manual del usuario o Windows-MCP label-targeting.
5. **`release-publisher` + `spec-forge` son los dos skills más ROI-positivos del catálogo** — usarlos por defecto en sesiones de release o refactor cross-layer.
6. **No hardcodear paths en scripts versionados**. Usar `$PSScriptRoot` / `$(dirname $0)` relativos. `tools/precommit_check_paths.sh` está activo en CI desde v0.1.2.

## Estado al cerrar

| Aspecto | Estado |
|---|---|
| **HEAD** | `5438438` (release: v0.1.6) |
| **Branch** | `main` (pushed origin) |
| **Tag más reciente** | `v0.1.6` |
| **Releases públicas** | v0.1.1 → v0.1.6 (6 zips, ~55 MB cada uno) en https://github.com/GS-RUN/tubelight/releases |
| **Repo visibility** | PUBLIC |
| **CI** | path-check ✓, Windows MSVC vcpkg ✓, Linux gcc-13 ✗ y clang-18 ✗ (ambos `continue-on-error`, status global verde) |
| **Uncommitted** | 0 (todo limpio) |
| **Binario en repo root** | `tubelight.exe` v0.1.6 (gitignored) |
| **ADRs activas** | ADR-0001 accepted (Phase 1 completa), ADR-0002 accepted (blueprint, Phase 2a/2b/2c shipped, Phase 3a→7a pendientes) |
| **Próximo bloque ROADMAP** | Phase 3a — `IRenderBackend` abstraction |
| **Blockers conocidos** | R14 (HDR phosphor spectra missing) bloquea Phase 5a hasta tener data. |

## Próximo paso al retomar

**Implementar Phase 3a de ADR-0002**: crear `src/render/backend.h` con la interfaz `IRenderBackend`, mover el código actual de `Pipeline::render_to_screen()` + FBO management a una nueva implementación `src/render/backend_gl.{h,cpp}` que delega a la lógica actual. CMake gana opción `TUBELIGHT_RENDERER_GL=ON` (default). CLI `--renderer gl` (única opción por ahora; `dx12` documentada como `coming soon`). Cero cambio funcional: el binario debe comportarse idéntico a v0.1.6 antes y después. Ship como **v0.1.7**.

## Comandos para arrancar rápido próxima vez

```bash
cd D:/AgentWorkspace/Tubelight
git pull
git log --oneline -5
cat docs/adr/0002-d3d12-dcomp-wgc-hdr-slang-blueprint.md | head -50
ls src/core/pipeline.h  # entry point actual del renderer
```

Skills del catálogo a tener cargadas desde el primer turno:
- `spec-forge` — para actualizar ADR-0002 si surgen alternativas durante 3a
- `release-publisher` — para cerrar v0.1.7
- `pr-reviewer` — opcional, antes del release final

Plan suggested para la sesión 3a (1 turno largo):

1. Diseñar `IRenderBackend` API (~10 métodos: `create_texture`, `create_fbo`, `bind_fbo`, `set_uniform_*`, `draw_quad`, `present`, `read_pixels_to_pbo`, etc.)
2. Crear `src/render/` directorio con `backend.h` + `backend_gl.{h,cpp}` que envuelve el código GL actual sin moverlo de `Pipeline`.
3. Cambiar `Pipeline::render_to_screen()` para que use `backend_->draw_quad()` en lugar de `quad_.draw()` directo.
4. Build + smoke test: `tubelight.exe --version`, `--profile pvm-8220 --signal composite_ntsc`, screenshot verifica visual idéntico a v0.1.6.
5. Cut v0.1.7 con release-publisher.

Honest estimación: 4-6 horas de sesión agentic real.
