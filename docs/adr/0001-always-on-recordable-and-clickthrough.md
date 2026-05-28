# ADR-0001 — Recordable y click-through como always-on; drop de Ctrl+Alt+R y Ctrl+Alt+C

**Fecha**: 2026-05-27
**Estado**: accepted
**Supersedes**: parte de PLAN.LOCKED.md F6 (input model windowed) y CONTRACTS.md §"Atajos globales"

## Contexto

v0.1.3 entrega tres flags de input controlados por hotkey:

- `Ctrl+Alt+R` toggle **recordable mode** — alterna entre captura por DXGI
  Desktop Duplication (con `WDA_EXCLUDEFROMCAPTURE` que esconde el overlay
  de Snipping Tool / Game Bar / OBS) y captura por Magnification API (con
  filtro per-window que permite que recorders externos vean el overlay
  sin entrar en feedback loop).
- `Ctrl+Alt+C` toggle **click-through** — alterna `WS_EX_TRANSPARENT` para
  que los clicks atraviesen la ventana hacia la app subyacente.
- `Ctrl+Alt+M` abre el menú in-app, que auto-desactiva click-through
  mientras el menú está visible.

Tres puntos motivan la revisión:

1. **UX confusa**: el usuario tiene que saber qué hotkeys pulsar antes de
   grabar con Snipping Tool, antes de jugar, etc. La aplicación de
   referencia que hemos comparado (ShaderGlass, Steam) no tiene este
   problema porque sus opciones equivalentes están "siempre on" por
   diseño.
2. **Bug latente en runtime attach**: `overlay_mode_win.cpp:674` fija
   `src_rect_` de la Magnification API al rect completo del monitor.
   Cuando el modo recordable está activo y `Ctrl+Alt+T` re-attachea a un
   nuevo target, el rect de captura no se actualiza — la imagen sigue
   capturando el área del monitor entero en vez del nuevo target. El bug
   pasa desapercibido porque el usuario normalmente no combina recordable
   con runtime attach.
3. **Complejidad innecesaria en settings y migración**: los campos
   `recordable` y `clickthrough_user` en `%APPDATA%\Tubelight\settings.json`
   requieren migración defensiva en cada arranque (forzar `recordable=false`
   per-session para evitar el bug, ignorar `clickthrough_user` viejos…).

## Decisión

1. **Recordable mode es always-on**.
   - `Ctrl+Alt+R` y su handler en el low-level keyboard hook (`g_hk_toggle_recordable`)
     se eliminan.
   - `g_recordable_mode.load()` se hardcodea a `true` durante toda la
     vida del proceso. El flag `g_recordable_mode` se mantiene como
     constante para no romper el compilation de `grab_source()` y otros
     consumers.
   - `WDA_EXCLUDEFROMCAPTURE` se elimina del binario. `apply_capture_affinity()`
     siempre llama `SetWindowDisplayAffinity(hwnd, WDA_NONE)`.
   - Magnification API se inicializa una vez al startup (no por toggle)
     y se usa como único capture backend.
   - DXGI Desktop Duplication se mantiene como fallback explícito
     (Mag callback no llega, MagInit falla) — degradación graceful, no
     toggle de usuario.
   - El campo `recordable` en `settings.json` se elimina. Migración:
     `load_settings()` ignora silenciosamente el campo si está presente.

2. **Click-through cross-process es always-on**.
   - `Ctrl+Alt+C` y su handler (`g_hk_toggle_clickthrough`) se eliminan.
   - El campo `clickthrough_user` en settings se elimina. Migración: idem.
   - `WS_EX_LAYERED + WS_EX_TRANSPARENT` se aplican al startup y nunca
     se quitan en runtime.

3. **Para conservar drag/resize de la ventana**, se introduce un sistema
   distinto:
   - **Chrome visible siempre**: title bar de ~24 px + bordes de ~4 px se
     renderizan dentro del framebuffer GL (no son ventanas Win32
     independientes; son geometría dibujada por el shader de composición
     final en el pass 6).
   - **Body click-through**: el área interior de la ventana, donde se
     renderiza el output CRT, es click-through cross-process.
   - **Drag/resize via low-level mouse hook**: `WH_MOUSE_LL` global. Si
     el cursor está sobre el rect del title bar / bordes Y LMB está
     pulsado, el hook intercepta los eventos, calcula deltas y mueve /
     redimensiona la ventana con `SetWindowPos`. Si el cursor está sobre
     el área interior, el hook deja pasar el evento (click-through).
   - **Cursor visual feedback**: cuando el cursor está sobre el title
     bar, el hook fuerza `IDC_ARROW`. Sobre bordes, `IDC_SIZEWE` /
     `IDC_SIZENS` / `IDC_SIZENWSE` / `IDC_SIZENESW` según el lado.
   - **Menú in-app**: `Ctrl+Alt+M` auto-desactiva click-through mientras
     está abierto (comportamiento ya implementado en `overlay_mode_win.cpp:2284`,
     se mantiene).

4. **Fix del bug Mag `src_rect_`**.
   - En el flujo `do_attach_target()` (`overlay_mode_win.cpp:1260`), tras
     `apply_capture_affinity(hwnd)`, llamar `mag_capture.set_source_rect(tx, ty, tw, th)`.
   - `MagCapture::set_source_rect()` (nueva API pública) actualiza
     `src_rect_` y emite `MagSetWindowSource(hwnd_mag_, src_rect_)`.
   - Análogamente en `do_attach_region()` y al salir de `do_toggle_fullscreen()`.

## Consecuencias

### Gana
- UX: zero hotkeys que el usuario tenga que conocer para "que el overlay
  funcione bien con OBS". Funciona de fábrica.
- Compatibilidad: Snipping Tool / Game Bar / OBS ven el overlay siempre.
- Codebase: ~80 LOC menos en `overlay_mode_win.cpp` (handlers + flags +
  setters de settings + ramas condicionales).
- Bug Mag `src_rect_` queda fixed como side effect.

### Rompe
- Settings.json viejos con `recordable` o `clickthrough_user` quedan
  obsoletos — los campos se ignoran. No requiere migración escrita
  porque `nlohmann/json::get_or<bool>("recordable", false)` simplemente
  no se consulta más.
- El manual §9.4 ("Recordable mode") se reescribe: no es ya un modo, es
  comportamiento por defecto. La explicación del WDA/Mag tradeoff se
  mueve a §13 troubleshooting (caso "tu app de captura no ve el
  overlay") y a la sección de lecciones aprendidas.
- §13.6 (Win+Shift+S checklist) y §13.7 (Game Bar checklist) se simplifican:
  desaparece el paso "did you toggle Ctrl+Alt+R?".

### Riesgo nuevo
- **R11**: Magnification API está marcada deprecated since Win10 en docs
  de Microsoft. Si MS la retira en una futura Windows 11/12, perdemos
  capture en ese build de Windows.
  - **Detección temprana**: smoke test que lanza tubelight headless y
    verifica que el callback de Mag fire dentro de 200 ms.
  - **Circuit breaker**: si MagInit falla o el callback no fire en 1 s,
    log warning + degradar a DXGI Duplication con WDA_NONE (el feedback
    loop reaparece pero el overlay sigue vivo).

## Alternativas consideradas

- **A: Chrome reveal-on-Alt** (cristal puro, ALT muestra title bar).
  Descartado porque el usuario priorizó comportamiento estándar Windows
  (chrome visible siempre).
- **B: Mouse-hover reveal** (chrome visible solo al pasar cursor por el
  borde). Descartado porque añade descubribilidad inferior (¿cómo sabe
  el user que hay chrome?) y porque la latencia del hover-detect en
  hooks low-level no es ideal.
- **C: Modo híbrido** (chrome visible en windowed, oculto en target/region).
  Descartado por incoherencia entre modos.
- **D: Mantener los hotkeys pero con default ON**. Descartado porque
  conserva la complejidad de settings + migración y no resuelve el bug
  Mag `src_rect_`.

## Referencias

- `overlay_mode_win.cpp:674` — bug Mag `src_rect_` documentado.
- `overlay_mode_win.cpp:1260-1293` — `do_attach_target()` actual.
- `overlay_mode_win.cpp:2078-2192` — handlers de hotkeys que se eliminan.
- ShaderGlass (https://github.com/mausimus/ShaderGlass) — referencia de
  UX always-on observada en Steam release.
- Memoria del usuario `project_tubelight.md` — lecciones aprendidas
  sobre WS_EX_LAYERED + WS_EX_TRANSPARENT cross-process click-through y
  el bug del Magnification API `src_rect_` hardcoded.
