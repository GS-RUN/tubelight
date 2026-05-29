# Session Debrief — 2026-05-29 — Tubelight

**HEAD**: `3ab6b08` (pushed `main`). 24 commits since `ceee2c0` (v0.2.0-beta debrief).
**Tag publicado**: `v0.2.0-rc.0` (prerelease).

## TL;DR

Sesión enorme: cerré **T5.5** (overlay WGC+DX12 zero-copy), endurecí el
backend DX12 (sync + descriptores), monté el **bench Phase 3e** (hallazgo:
el gap ShaderGlass es la captura, no el shader), e hice casi toda la
**Phase 4a** (DComp + menú ImGui DX12 + audio/vsync/recordable + i18n ES/EN
pass 1). **PERO el click-through cross-process del overlay DX12 SIGUE SIN
FUNCIONAR** pese a 3 intentos — es el bloqueo #1 para la próxima sesión.

## ⛔ BLOQUEO #1 — click-through DX12 NO funciona (prioridad máxima)

**Síntoma (confirmado por usuario)**: con el overlay DX12 fullscreen y el
menú cerrado, los clicks NO atraviesan a la app de debajo (un emulador no
recibe foco/órdenes). En el **path GL esto SÍ funciona** (shipped).

**Caso de uso**: emulador debajo + overlay CRT encima → el emulador debe
responder al ratón/teclado. Es **primordial** (palabra del usuario).

**Lo que se probó y NO resolvió** (en orden):
1. `WS_EX_TRANSPARENT` solo sobre la ventana GLFW → no pasa clicks.
2. `WS_EX_LAYERED | WS_EX_TRANSPARENT` → pasa clicks PERO rompe el display
   DComp (overlay 100% transparente, verificado por screenshot — la
   superficie DWM layered tapa el visual DComp). Mutuamente excluyentes.
3. **Ventana Win32 cruda con `WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT`**
   (commit `3ab6b08`, reemplazó GLFW por CreateWindowEx + ImGui_ImplWin32) →
   **el display funciona** (glow ámbar del bezel confirmado por screenshot)
   pero **el click-through SIGUE sin funcionar** según el usuario. ← AQUÍ.

**Restricciones técnicas confirmadas**:
- D3D12 EXIGE flip-model swap chain (FLIP_DISCARD/SEQUENTIAL). **BitBlt
  (DISCARD clásico) es imposible** con D3D12 → no se puede usar el truco
  layered+BitBlt del path GL directamente.
- flip-model + `WS_EX_LAYERED` son incompatibles → por eso se fue a DComp
  (composition swap chain, `CreateSwapChainForComposition` + visual tree).
- `WS_EX_LAYERED` + DComp-CreateTargetForHwnd → la superficie layered tapa
  el visual (no se ve nada).

**Hipótesis a probar la próxima sesión (ordenadas por prometedoras)**:
1. **WM_NCHITTEST → HTTRANSPARENT en `dx12_wndproc`**: aunque la memoria dice
   que HTTRANSPARENT es same-thread-only, COMBINADO con WS_EX_TRANSPARENT en
   una ventana de composición puede ser lo que falta. Es 5 líneas — probar
   primero. (Quizá el NOREDIRECTIONBITMAP necesita ayuda explícita en el
   hit-test.) Devolver HTTRANSPARENT salvo cuando el menú está abierto.
2. **Verificar que WS_EX_TRANSPARENT está realmente aplicado** en runtime
   (GetWindowLongPtr tras crear) — quizá un flag se pierde o el toggle del
   menú lo deja mal. Loguear el ex-style real cada toggle.
3. **`SetWindowRgn` a región vacía / `DwmExtendFrameIntoClientArea`** — otra
   técnica de click-through para composición.
4. **Camino alternativo robusto (si lo anterior falla)**: renderizar el
   pipeline DX12 a un RT offscreen y presentar a un **swap chain D3D11
   BitBlt sobre una ventana LAYERED** vía el `D3D11On12` device que YA
   tenemos (para WGC). D3D11 sí soporta BitBlt+layered → replica EXACTO lo
   que hace el path GL (que funciona). Más trabajo pero conocido-bueno.
5. **Reconsiderar**: el path GL ya da click-through + menú + todo. ¿Merece
   la pena DX12 para el emulador, o el usuario usa GL para ese caso y DX12
   para "ver" (vídeo/escritorio)? Preguntar. (DX12 = zero-copy 3.4ms menos
   CPU/frame, medido — ver PHASE_3E_BENCH.md.)

**Arranque rápido para depurar** (con ratón, es interactivo):
```
tubelight.exe --overlay-fullscreen --renderer dx12 --profile pvm-8220 --signal composite_ntsc
# menú cerrado → clicar app de debajo. TUBELIGHT_D3D12_DEBUG=1 para validation.
# Código: src/overlay/overlay_mode_win.cpp → run_dx12() (~L1032) + dx12_wndproc (~L1040).
```

## Qué se entregó esta sesión (24 commits, todo en `main`)

- **T5.5** (`3b21001`/`62e17a3`/`e125ddd`): overlay WGC+DX12 zero-copy
  (`--renderer dx12 --overlay*`), hotkeys globales, no roba foco, tracking.
- **v0.2.0-rc.0** (`af680a6`): release prerelease pública cortada.
- **Sync DX12** (`00c0ebb`): N-frame-in-flight (sin wait-per-Present). Bit-exact.
- **Bench Phase 3e** (`a1db1e1`/`183ca5c`/`30d8b69`): `--bench` GPU-timestamp.
  HALLAZGO: pipeline GL≈DX12 (~0.38ms paridad); **captura→GPU GL 3.385ms vs
  DX12-WGC 0.003ms (~1000×)** = el gap ShaderGlass real. Cacé/corregí un
  artefacto de vsync. Refactor descriptores persistentes (bit-exact).
  Detalle: `docs/perf/PHASE_3E_BENCH.md`.
- **Phase 4a.1** (`b13114f`): composition swap chain + DComp.
- **Phase 4a.3** (`01ab116`/`3862786`): menú ImGui en DX12 (imgui dx12-binding).
- **Menú host-controls** (`8c8a9d9`): audio (CrtAudio), vsync, recordable.
  Sliders CRT + perfil/señal ya iban (editan pipeline.params() directo).
- **Auto-sync exe raíz** (`49972e6`): POST_BUILD copia exe+dxil a la raíz en
  cada build (el exe raíz estaba STALE en v0.1.6 — el usuario siempre lo usa).
- **Fixes menú DX12**: folder picker (`502b8a4`/`ec79a42` — STA thread,
  colgaba por apartamento WinRT/MTA), ocultar consola.
- **i18n ES/EN pass 1** (`e9bb091`): autodetect SO + toggle arriba-derecha +
  pestañas + cabeceras traducidas.
- **Rework Win32** (`3ab6b08`): ventana NOREDIRECTIONBITMAP (intento de
  click-through, display OK pero click-through aún no).

## Otros pendientes (no bloqueantes)

- **i18n pass 2** (tarea #12): ~70 cadenas restantes del menú (sliders/
  botones/combos + variantes EN de los ~40 tooltips ES). Patrón: `T(en,es)`
  en menu.cpp.
- **Menú DX12: video MP4 + HUD** diferidos (necesitan readback DX12 / HUD path).
- **v0.2.0 stable**: el gate visual ya se cumplió (display/menú OK); falta
  click-through para que el overlay DX12 esté "completo". Cortar cuando 4a OK.
- **Región/windowed crop** (WGC es granularidad de monitor) + **target size
  tracking** (WGC pool recreate) → diferidos a v0.2.1.
- **DPI/HiDPI** DX12 no validado.

## Estado verificado (RTX 2080 Ti FL 12_2)
Display CRT ✓, menú ImGui DX12 ✓ (verificado por usuario: clickable, perfil/
señal en vivo), audio whine ✓, salida limpia Ctrl+Alt+Q ✓, golden shader-only
bit-exact ✓, GL path intacto ✓. **Click-through ✗ (abierto).**

## Skills usadas
`dx12-engineer` (T5.5/sync/4a-debug/composition-window), `cpp-perf-refactor-
playbook` (bench 3e). Próxima sesión: `dx12-engineer mode=debug` para el
click-through (cruzar contra hipótesis arriba) + `native-debugger` si hay
algo de input routing Win32.

## Comandos primer turno (próxima sesión)
```
cd D:/AgentWorkspace/Tubelight && git pull && git log --oneline -5
cat docs/sessions/SESSION_DEBRIEF_2026_05_29.md   # este doc
# Atacar BLOQUEO #1 (click-through) con la hipótesis 1 (WM_NCHITTEST) primero.
```
