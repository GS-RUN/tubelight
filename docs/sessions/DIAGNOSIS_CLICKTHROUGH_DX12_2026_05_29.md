# Diagnosis — DX12 overlay cross-process click-through (BLOQUEO #1)

**Skill**: `dx12-engineer` mode=debug · scope=swap-chain/window · target_file
`src/overlay/overlay_mode_win.cpp` (`run_dx12` ~L1073, `dx12_wndproc` ~L1048).
**Fecha**: 2026-05-29 · **HEAD base**: `3ab6b08`.
**Síntoma (usuario)**: overlay DX12 fullscreen, menú cerrado → los clicks NO
atraviesan a la app de debajo (un emulador no recibe foco/ratón). En el path
**GL el mismo caso SÍ funciona** (shipped).

---

## 1. Causa raíz (alta confianza)

El click-through **cross-process** en Win32 lo da **un único mecanismo**:
una ventana **layered** (`WS_EX_LAYERED`) con `WS_EX_TRANSPARENT`. En cuanto
la ventana es *no-layered*, `WS_EX_TRANSPARENT` por sí solo **no** la excluye
del hit-test del sistema → los clicks caen en el overlay, no en la app de
abajo. Y `WM_NCHITTEST → HTTRANSPARENT` re-rutea **solo dentro del mismo
hilo** (MSDN, literal) → inútil cross-process.

Esto NO es teoría: es lo que demuestra el propio código de Tubelight, A/B,
en el mismo binario y misma máquina:

| Path | Estilos de la ventana | Display | Click-through |
|---|---|---|---|
| **GL** (shipped, funciona) | `WS_EX_LAYERED \| TRANSPARENT` + `SetLayeredWindowAttributes(0,255,LWA_ALPHA)` — `overlay_mode_win.cpp:1684-1687` | ✅ (GL presenta vía GDI/DWM → entra en la superficie layered) | ✅ |
| **DX12 intento 2** | `WS_EX_LAYERED \| TRANSPARENT` | ❌ negro (la superficie DWM layered tapa el visual DComp) | ✅ |
| **DX12 actual** `3ab6b08` | `WS_EX_NOREDIRECTIONBITMAP \| TRANSPARENT` — `overlay_mode_win.cpp:1152-1153` | ✅ (DComp dueño de los píxeles) | ❌ ← **aquí** |

La contradicción está en el **conflicto de superficie**:

- **Display DComp** exige `WS_EX_NOREDIRECTIONBITMAP` → la ventana **no tiene
  superficie de redirección** → es **no-layered** → `WS_EX_TRANSPARENT` no
  cruza clicks.
- **Click-through cross-process** exige `WS_EX_LAYERED` → la ventana **tiene
  superficie de redirección DWM** → tapa el visual DComp → negro.

`WS_EX_LAYERED` y `WS_EX_NOREDIRECTIONBITMAP` son **mutuamente excluyentes**
sobre el mismo HWND. Ese es el nudo.

### Por qué GL sí puede y DX12 no (la asimetría clave)

OpenGL presenta por la ruta **GDI/opengl32 → DWM**, que DWM **redirige a la
superficie layered**. Por eso GL convive con `WS_EX_LAYERED`: los píxeles del
shader entran *en* esa superficie. D3D12 **exige flip-model** swap chain
(`FLIP_DISCARD`/`SEQUENTIAL`), que presenta **bypasseando** la superficie de
redirección → con `WS_EX_LAYERED` la superficie queda transparente (negro);
sin él (NOREDIRECTIONBITMAP) el visual se ve pero no hay layered → no hay
click-through. **BitBlt clásico no existe en D3D12** (confirmado).

---

## 2. Por qué las hipótesis "baratas" del debrief probablemente NO bastan

Mapeo honesto contra el mecanismo Win32 antes de gastar una sesión en cada una:

- **Hyp #1 — `WM_NCHITTEST → HTTRANSPARENT` en `dx12_wndproc`**: el wndproc
  DX12 (`:1048`) hoy **no maneja `WM_NCHITTEST`** en absoluto (a diferencia del
  subclass GL `tubelight_subclass_proc:302`). Pero HTTRANSPARENT es
  **same-thread-only** → no cruza al proceso del emulador. **Además**: si la
  ventana lleva `WS_EX_TRANSPARENT`, el SO **ni siquiera envía `WM_NCHITTEST`**
  (la salta). Para que el probe NCHITTEST tenga sentido hay que crear la
  ventana **sin** `WS_EX_TRANSPARENT`. Predicción: no cruza procesos. **Vale la
  pena confirmarlo empíricamente porque cuesta 5 líneas** — pero no apostar a él.
- **Hyp #2 — verificar que `WS_EX_TRANSPARENT` está aplicado en runtime**:
  imprescindible como *sanity check* (descarta que el toggle de menú o `no_ct`
  lo dejen mal). El toggle abrir/cerrar menú (`:1402-1411`) es simétrico, pero
  hay que **verlo**. Lo instrumento (coste cero).
- **Hyp #3 — `SetWindowRgn` región vacía**: en un HWND con target DComp
  (`CreateTargetForHwnd`), DWM **clipa el visual DComp a la región de la
  ventana** → región vacía = visual invisible = mismo callejón que el negro.
  Riesgo alto de romper el display. Lo dejo como probe env-gated para
  *confirmar* la teoría, no como fix.

**Conclusión**: las tres son **probes diagnósticos**, no soluciones probables.
Sirven para obtener *ground truth* con ratón en la RTX y cerrar el debate.

---

## 3. La solución robusta (Path B — conocido-bueno)

Si los probes de §2 fallan (lo esperado), la arquitectura correcta es
**replicar la receta GL probada** manteniendo la ventaja real de DX12 (la
captura WGC zero-copy, que es donde está el ~1000× — `PHASE_3E_BENCH.md`):

> **Ventana `WS_EX_LAYERED | WS_EX_TRANSPARENT` + presentar el frame final por
> GDI** (`UpdateLayeredWindow` / BitBlt de un DIB con readback del RT) en vez
> de por flip-model/DComp.

Pipeline propuesto (reusa piezas que ya existen):

1. Render del 8-pass en D3D12 a un **RT offscreen** (ya tenemos RTs intermedios).
2. **Readback** del RT final a un DIB top-down BGRA (un `D3D12_HEAP_TYPE_READBACK`
   + copy, patrón DX-13/DX-06). Tamaño = overlay (no desktop).
3. `UpdateLayeredWindow(hwnd, ..., ULW_ALPHA)` con ese DIB sobre la ventana
   `WS_EX_LAYERED | WS_EX_TRANSPARENT`.

**Trade-off honesto (no ocultárselo al usuario)**: esto reintroduce **un
readback por frame en el lado de presentación** (~3 ms a 1920×1200, el mismo
orden que el readback de captura del path GL). PERO la captura sigue siendo
WGC zero-copy, así que el coste total ≈ GL **solo en presentación** y mucho
mejor en captura. No es el "zero-copy ambos lados" de DComp, pero es la única
ruta Win32 que da click-through cross-process + contenido D3D garantizado.

Variante a evaluar (futuro): presentar vía el device **D3D11On12 que ya
tenemos** a un swap chain D3D11; pero DXGI (incluido BitBlt D3D11) tampoco
compone en superficie layered de forma fiable → `UpdateLayeredWindow` con
readback es lo seguro. Documentar resultado.

---

## 4. Plan de acción (orden)

1. **[este patch]** Instrumentar ex-style en runtime + 3 probes env-gated
   (NCHITTEST sin TRANSPARENT / región vacía / log). Riesgo nulo, default sin
   cambio. → el usuario A/B-testea con ratón en la RTX y reporta cuál (si
   alguno) **pasa clicks Y mantiene display**.
2. **Si algún probe gana** → Path A, lo consolidamos (cosa rara pero ideal).
3. **Si todos fallan** (esperado) → implementar **Path B** (layered +
   `UpdateLayeredWindow` con readback) en una sesión propia, con golden visual
   + verificación de click-through con app real debajo. Borrar el dead-code
   `g_clickthrough_effective`/HTTRANSPARENT del path GL (engañoso: la línea
   `:297` afirma que funciona sin LAYERED, falso — el GL real usa LAYERED).

---

## 5. Cómo testear (interactivo, requiere ratón + app debajo)

```bat
:: app de debajo: abrir un emulador / Notepad / navegador a pantalla completa, luego:
set TUBELIGHT_D3D12_DEBUG=1
tubelight.exe --overlay-fullscreen --renderer dx12 --profile pvm-8220 --signal composite_ntsc
:: menú cerrado → clicar la app de debajo. Mirar stderr: imprime el ex-style real.

:: Probe A (NCHITTEST, sin WS_EX_TRANSPARENT):
set TUBELIGHT_CT_NCHITTEST=1
tubelight.exe --overlay-fullscreen --renderer dx12 ...

:: Probe B (región vacía — probablemente rompe el display, lo confirmamos):
set TUBELIGHT_CT_EMPTYRGN=1
tubelight.exe --overlay-fullscreen --renderer dx12 ...
```

Para cada probe reportar dos cosas: **(1) ¿se ve el CRT?  (2) ¿pasan los
clicks a la app de debajo?** Con esa matriz cerramos Path A vs Path B sin más
teoría.

---

## 6. Evidencia recogida esta sesión (instrumentación)

**Smoke RTX 2080 Ti** (`--overlay-fullscreen --renderer dx12`, default sin
probes), stderr:

```
[overlay] dx12: EXSTYLE=0x082000a8 { NOREDIRECTIONBITMAP TRANSPARENT NOACTIVATE TOPMOST } click-through=WS_EX_TRANSPARENT
[tubelight][d3d12] DirectComposition visual tree ready
[tubelight][d3d12] init OK on ... RTX 2080 Ti, FL 12_2 (1920x1200, 2 backbuffers)
[overlay] dx12: first frame rendered
```

- **Hyp #2 ELIMINADA**: `0x082000a8` = NOACTIVATE(0x08000000) +
  NOREDIRECTIONBITMAP(0x00200000) + TOOLWINDOW(0x80) + TRANSPARENT(0x20) +
  TOPMOST(0x08). `WS_EX_TRANSPARENT` **sí está aplicado** en runtime. El fallo
  de click-through **no** es un flag perdido → confirma la causa raíz §1
  (TRANSPARENT sobre ventana no-layered no cruza procesos). Display + DComp +
  WGC OK, sin crash con la instrumentación.
- **Probes A/B**: el usuario confirmó que NO funcionan → se descartó Path A.

---

## 7. Path B IMPLEMENTADO (layered ULW present)

Confirmado por el usuario que los probes fallan → implementado Path B.

**Backend** (`backend.h` / `backend_d3d12.{h,cpp}`): nuevo
`BackendInitParams::layered`. Renderiza a un **composition swap chain** (no
HWND-bound — `CreateSwapChainForHwnd` prohíbe ventanas `WS_EX_LAYERED`) pero
**sin árbol DComp de display**. El overlay lee cada frame con
`capture_backbuffer()`. Mutuamente excluyente con `composition`.

**Overlay** (`overlay_mode_win.cpp::run_dx12`): ventana borderless ahora
`WS_EX_LAYERED | WS_EX_TRANSPARENT | NOACTIVATE | TOOLWINDOW | TOPMOST`
(se quitó `NOREDIRECTIONBITMAP`). Tras `end_frame()`, `present_layered()`:
`capture_backbuffer` (RGBA8) → DIB top-down 32bpp con swap R↔B + alpha=255
(opaco) → `UpdateLayeredWindow(... ULW_ALPHA, AlphaFormat=0, SrcConstAlpha=255)`.
Réplica exacta del recipe GL (LAYERED+TRANSPARENT+alpha 255 opaco), con la
captura WGC zero-copy intacta. Probes/NOREDIRECTIONBITMAP/handler NCHITTEST
eliminados.

**Trade-off**: 1 readback/frame en presentación (`capture_backbuffer` hace
`wait_for_gpu_idle` → 1-frame-in-flight). Aceptable para overlay; optimizable
luego (swizzle R↔B en GPU + readback persistente sin asignar por frame).

**Verificado headless (RTX 2080 Ti)**: build limpio 0 err/warn;
`EXSTYLE=0x080800a8 { TRANSPARENT LAYERED NOACTIVATE TOPMOST }`; "layered ULW
present mode"; **`capture_backbuffer` mean-rgb=22.8** (frame real del
escritorio procesado por CRT, NO negro → capture+render+DIB OK); 0 errores de
validación D3D12; la ventana layered ocupa el monitor (ULW pinta, no se ve el
escritorio detrás). **PENDIENTE (interactivo, usuario)**: confirmar con ratón
que los clicks atraviesan a un emulador debajo + OK visual del CRT en vivo.
El mecanismo es idéntico al path GL probado, así que debería cruzar.

---

## 8. CORRECCIÓN — ULW swallow clicks → SLWA + BitBlt (el fix real)

Primera implementación de Path B usaba `UpdateLayeredWindow` (ULW). El usuario
confirmó: **display OK pero los clicks SEGUÍAN sin pasar**. Causa: en ventanas
ULW el hit-test del ratón lo gobierna el **canal alpha del bitmap** — como el
overlay es opaco (alpha=255, necesario para verse), captura todos los clicks, y
`WS_EX_TRANSPARENT` no lo override. GL nunca tuvo este problema porque usa
`SetLayeredWindowAttributes` (SLWA), donde el hit-test es por **rectángulo** y
`WS_EX_TRANSPARENT` sí da pass-through.

**Fix**: replicar GL exactamente → `SetLayeredWindowAttributes(0,255,LWA_ALPHA)`
+ pintar el frame con **GDI BitBlt al DC de la ventana** (no ULW). Verificado:
el overlay CRT se ve sobre Notepad vía `--overlay-target` (BitBlt+SLWA pinta), y
el hit-test es ahora idéntico al GL probado. **LECCIÓN**: para click-through en
ventana layered, usar SLWA (no ULW) — ULW hit-testea por alpha y se traga los
clicks de los píxeles opacos.
