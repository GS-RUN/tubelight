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

## 12. AUDITORÍA (2026-05-30) — NO se reproduce diferencia GL/DX12

Auditoría a fondo del supuesto bug DX12 con `dx12-engineer` mode=debug.
**Hallazgo incómodo: el test que dio "GL=1 / DX12=0" (§11) era INVÁLIDO.**
`Shell.Document.SelectedItems()` devolvía 0 **aunque el archivo estuviera
visiblemente seleccionado** (confirmado por screenshot de la barra de estado:
"1 elemento seleccionado" mientras el COM decía 0). Las conclusiones de §11
quedan retractadas.

Re-medido con método FIABLE (clic a través del overlay → matar overlay →
screenshot de la barra de estado del Explorer, que persiste la selección):
- **Ventana objetivo ENFOCADA**: GL → selecciona ✓ · **DX12 → selecciona ✓**.
  Ambos pasan el clic.
- **Ventana objetivo SIN foco** (Notepad enfocado, movimiento real + doble-clic):
  GL → NO selecciona · DX12 → NO selecciona. **Ambos fallan igual.**
- **Bisección BitBlt** (`TUBELIGHT_SKIP_PRESENT`): sin BitBlt el resultado es el
  mismo → **el BitBlt NO es la causa**.
- **Probe de cursor** (`GetCursorInfo`): inconcluso (baseline ya daba ARROW) pero
  GL == DX12.

**Conclusión**: GL y DX12 usan el MISMO mecanismo Win32 (`WS_EX_LAYERED|
TRANSPARENT` + SLWA) y se comportan IDÉNTICO en todos los tests controlados.
**No se reproduce la diferencia GL/DX12 del usuario por inyección de input.**
Hipótesis principal del por qué el usuario la percibe: el bug de **imagen
congelada** de DX12 (ahora arreglado, §10) — con el CRT congelado los clics
funcionaban pero eran INVISIBLES → "DX12 no atraviesa"; GL (vivo) sí se veía.
El patrón "ventana sin foco no recibe el clic" afecta a AMBOS y es ortogonal
(clic a ventana inactiva bajo overlay topmost NOACTIVATE).

**SIGUIENTE PASO decisivo (necesita al usuario)**: re-test de DX12 con el build
ACTUAL (imagen ya viva) + si sigue fallando, capturar `tubelight_clickthrough.log`
de su intento REAL (¿aparece `WM_LBUTTONDOWN`? → overlay captura; ¿no? → pasa).
Es la única forma de cerrar el gap real-mouse vs inyectado. Launcher se mantiene
en **GL** (confirmado funcionando para el usuario) hasta validar DX12 en vivo.

## 11. NUEVO BLOQUEO (2026-05-30) — DX12 click-through NO entrega clics (GL sí) [RETRACTADO §12]

Con la captura ya viva, el usuario seguía sin poder interactuar. Test
discriminante con `Shell.Document.SelectedItems()` (inyectando clic sobre un
archivo de Explorer a través del overlay):
- **GL overlay** (`--overlay-fullscreen`): clic → **`sel=1`** (el archivo SE
  selecciona). Click-through REAL funciona.
- **DX12 overlay** (`--renderer dx12`): clic → **`sel=0`** (NO se selecciona).
  El overlay NO captura el clic (0 `WM_*BUTTON` en el log) PERO el clic tampoco
  llega a actuar en la ventana de debajo → se "pierde".

O sea: **pese a usar los mismos estilos (`WS_EX_LAYERED|TRANSPARENT` + SLWA
LWA_ALPHA 255), el click-through de GL entrega los clics y el de DX12 no.** La
diferencia está en cómo se presenta el frame a la ventana layered:
- GL: render OpenGL → SwapBuffers (DWM redirige a la superficie de la ventana).
- DX12: readback + **BitBlt a `GetDC(hwnd)`** cada frame.

**Hipótesis principal (a verificar)**: el `BitBlt` continuo a la DC de la
ventana SLWA deja la ventana en un estado donde DWM no enruta los clics ni
arriba (a nuestro wndproc) ni abajo (a la app). El `WS_EX_TRANSPARENT` deja de
surtir efecto con ese método de pintado. Alternativas a probar mañana:
1. Presentar el frame DX12 de otra forma compatible con click-through: pintar
   por **WM_PAINT** (invalidar + BitBlt en el handler) en vez de BitBlt directo
   en el loop; o `UpdateLayeredWindow` con **alpha por-píxel 255** (probar si en
   ESTE caso WS_EX_TRANSPARENT sí domina el hit-test) — antes se descartó pero
   con el dato nuevo merece re-test medido con `SelectedItems`.
2. Crear la ventana DX12 EXACTAMENTE como la de GL (misma clase/estilos GLFW)
   o reusar el window-management del path GL.
3. Comparar con un mini-repro: ventana SLWA + BitBlt vs SLWA + GL, medir
   selección.

**ESTADO**: launcher vuelto a **GL** (funciona end-to-end: captura viva +
click-through + selección, verificado `sel=1`). DX12 = captura zero-copy OK
pero **click-through roto** → bloqueo para la próxima sesión. La fix de WGC
(§10) sigue siendo válida e independiente.

## 10. RESUELTO (2026-05-30) — WGC freeze = recycle roto en device 11On12

Causa raíz **probada**: WGC **no recicla los buffers del pool sobre un device
D3D11On12**. Diagnóstico decisivo: con el consumidor mínimo (sample MS,
auto-dispose del frame) el callback `FrameArrived` para en **exactamente
BufferCount** (2 buffers→cb#2, 4 buffers→cb#4); al crear un **device D3D11
plano dedicado** para WGC, el callback dispara sin parar (cb#1..14+). No es
oclusión, ni "nada cambia", ni mi lógica de consumo — es el device.

**Fix (`wgc_capture.cpp`)**: WGC corre en su **propio device D3D11 hardware**
(`create_plain_d3d11_device`) donde el reciclaje funciona. Cada frame se copia
a una **textura SHARED triple-buffer** en ese device y se **abre por handle
compartido en el device 11On12** del consumidor (`OpenSharedResource`), de modo
que el pipeline DX12 la muestrea **sin readback CPU** (GPU-GPU, zero-copy
preservado). El callback solo parquea el frame más reciente; el hilo principal
copia + lo cierra (libera el buffer del pool). **Verificado RTX 2080 Ti**:
`WGC: frame_count` crece ~45-60/s de forma continua; imagen viva y correcta
(CRT sobre target window, screenshot); 0 errores. **DX12 ahora completo:
captura viva zero-copy + click-through.** Launcher vuelto a `--renderer dx12`.

## 9. El síntoma "no funciona" del usuario = imagen CONGELADA (no el click)

Tras todo lo anterior, el usuario confirmó que **el clic SÍ atraviesa** (su log
mostró el foreground cambiando KegaClass↔Explorador al clicar a través del
overlay) **pero la imagen del CRT está congelada** → parecía que el clic no
hacía nada.

**Causa**: el path DX12 captura con **WGC**, y WGC **deja de entregar
`FrameArrived` tras 1-2 frames** en este montaje (D3D11On12 + overlay). Medido
con instrumentación (`TUBELIGHT_CT_LOG=1` → `WGC: frame_count=...`):
`frame_count` se clava en 1-2 y no crece, tanto en fullscreen como en modo
ventana (descartada oclusión y "nada cambia"). Intentos: `Create`→
`CreateFreeThreaded` (1→2), cerrar el frame anterior para reciclar buffers
(sigue 2), sondeo en hilo principal (1). El pool simplemente deja de producir
— problema de fondo del path WGC+11On12 que requiere sesión propia (probable:
copiar el frame a textura propia con `ID3D11Multithread` + double-buffer, o
volver a DXGI Duplication como el path GL).

**Resolución para el usuario AHORA**: usar el **renderer GL** (`--overlay-
fullscreen`, sin `--renderer dx12`). GL captura con **DXGI Desktop Duplication
+ Magnification API** → **imagen viva** (verificado: 2035 puntos de muestra
cambian entre dos capturas) **y click-through** (WS_EX_LAYERED|TRANSPARENT,
verificado pasa clics). El lanzador `Overlay-ClickThrough.bat` ahora usa GL.
DX12+WGC queda como mejora pendiente (zero-copy) una vez resuelta la captura
continua.

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
