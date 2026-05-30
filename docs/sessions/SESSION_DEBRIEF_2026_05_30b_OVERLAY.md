# Session Debrief — 2026-05-30 (b) — Tubelight overlay DX12 ventana

## TL;DR
- **Intent**: que el modo ventana DX12 iguale a **ShaderGlass** — bajo lag +
  click-through + resize suave.
- **Reality**: conseguido casi todo vía la **técnica ShaderGlass** (present
  DIRECTO sobre ventana layered, NO BitBlt). CRT visible (confirmado), bajo lag,
  click-through (Ctrl), resize sin vibrar. **Queda 1 bug abierto**: "vibra
  cuando muevo el ratón".
- **Próximo paso al retomar**: atacar el jitter al mover el ratón — hipótesis #1
  primero (vsync OFF `Present(0)` → tearing). Ver §Tasks.

## Intent vs Reality
- **Intent**: arreglar la vibración del overlay DX12 ventana para igualar a
  ShaderGlass.
- **Reality**: tras ~19 commits, DX12 ventana logra bajo lag + click-through +
  resize suave. La **divergencia clave y más valiosa**: durante medio día asumí
  (heredado del saga) que "DX12 layered = invisible / hay que BitBlt el readback"
  → eso causaba el LAG y la VIBRACIÓN. Investigar **ShaderGlass (open source)**
  reveló que se presenta DIRECTO sobre una ventana layered (sin BitBlt) y **eso
  SÍ funciona en D3D12** (lo confirmó el usuario: "CRT y vibra"). Todo el
  sufrimiento previo venía de la técnica de present equivocada.

## Qué se hizo (19 commits, +592/-126 LOC en 7 archivos)
Evolución (cada uno intento de la vibración, hasta dar con la técnica correcta):
- `a1b6916`→`78bdbff`: dos-ventanas (rechazado) → una ventana + Ctrl-move.
- `90ec22c`: reafirmar WDA tras swaps (gotcha NVIDIA+Win11).
- `130bdbd`: readback persistente (30→58fps) — la captura no era el cuello.
- `f631c24`/`e704211`: free-list de slots RTV/SRV + **invalidar caché baked**
  (causaba DEVICE_HUNG/TDR al reciclar slots con mismo `.ptr`).
- `1d6e831`/`c6a3bd6`/`8b902fe`/`caf9654`: saga del resize (cero-D3D, vivo,
  diferido, no-present-durante-modal).
- `3bfc257`: gate de `magnetic_interference` por `glass_age` (wobble del shader).
- `ef47f2c`: AUDIT GL vs DX12 (WGC no capta vídeo MPO; GL sí).
- `6d17f02`: **técnica ShaderGlass** (CreateSwapChainForHwnd + runtime
  WS_EX_LAYERED + present directo). **El punto de inflexión.**
- `92df7a5`: `DXGI_SCALING_STRETCH` + diferir ResizeBuffers.
- `caf9654`: no presentar durante el resize modal (DWM escala el último frame).
- `185114e`: `IsCursorCaptureEnabled(false)` (NO arregló el jitter del ratón).

## Lecciones aprendidas (no obvias)
1. **D3D11 bitblt-model vs D3D12 flip-model en ventana layered (CT-7 en la
   skill).** ShaderGlass (D3D11, `DXGI_SWAP_EFFECT_DISCARD`) presenta a la
   superficie de **redirección**, que una ventana layered SÍ compone → directo,
   bajo lag, sobre click-through. D3D12 SOLO flip-model (bypassea redirección).
   **PERO** —contra lo que decía el saga— el present **directo flip-model SÍ se
   ve** en una ventana layered D3D12 (creada NON-layered + `WS_EX_LAYERED`
   añadido en runtime + `SetLayeredWindowAttributes(255)`). NO hace falta BitBlt.
2. **El BitBlt del readback era la causa del LAG Y de la vibración** — no un mal
   necesario. El present directo elimina ambos.
3. **Caché de descriptores per-pass keyea por `.ptr` de slot** → una free-list
   que reusa slots con el mismo `.ptr` para un recurso nuevo = falso cache-hit =
   GPU samplea recurso liberado = `DEVICE_HUNG`. Invalidar la caché al reciclar.
4. **No presentar (flip-model) durante el resize modal**: presentar frames
   nuevos mientras Windows redimensiona flickea. Dejar a DWM escalar el último
   frame (`DXGI_SCALING_STRETCH`), repintar al soltar (como toda app normal).
5. **El lag de GL es la CAPTURA (Mag/DXGI ~3.4ms), NO el present.** El present de
   GL (OpenGL→redirección) compone suave en layered. Para paridad ShaderGlass en
   GL bastaría cambiar su captura a WGC (vía WGL_NV_DX_interop).
6. **NVIDIA+Win11: un swap de `GWL_EXSTYLE` tira `WDA_EXCLUDEFROMCAPTURE`** →
   auto-captura → feedback. Reafirmar WDA tras cada swap.

## Decisiones tomadas
| Decisión | Alternativas descartadas | Trade-off |
|---|---|---|
| Técnica ShaderGlass: present directo sobre ventana layered runtime | BitBlt readback (lag+vibración); dos ventanas (overhead, rechazado); GL default (lag de captura) | Requiere Win10 2004+ (en viejas → ventana negra) |
| Esquema invertido: sin tecla=arrastrar/resize, Ctrl=click-through | Ctrl=mover (rechazado por el usuario); siempre click-through (no se puede arrastrar) | Ctrl es modificador, no toggle |
| `Present(0)` vsync OFF para bajo lag | Present(1) (más lag) | **Sospechoso del jitter al mover ratón — revisar mañana** |
| Default renderer = DX12 | GL default (revertido: GL tiene lag de captura) | DX12 no capta vídeo MPO (C-1); usuario lo sabe |

## Tasks diferidos → TodoHub
- [ ] **BUG: "vibra cuando muevo el ratón" en DX12 ventana.**
  - Qué falta: el contenido tiembla SOLO al mover el ratón (quieto = OK). Resize
    ya NO vibra; visibilidad/lag/click-through OK.
  - Probado y NO funcionó: `IsCursorCaptureEnabled(false)` (hipótesis doble-cursor).
  - Hipótesis ordenadas para mañana:
    1. **`Present(0)` vsync OFF (que metí para bajo lag) → tearing visible solo
       cuando el contenido cambia (= al mover ratón).** Probar `set_vsync(true)`
       (Present(1)) en win_mode, o swap chain con `ALLOW_TEARING` + medir. **#1.**
    2. Verificar que `IsCursorCaptureEnabled(false)` REALMENTE aplicó (¿el
       try/catch tragó algo? ¿la proyección winrt expone el setter?).
    3. WGC entrega frames más rápido al moverse el ratón (dirty regions) →
       jitter de pacing en render_one/main loop.
    4. La ventana en default (no-transparent) recibe `WM_MOUSEMOVE`/`WM_SETCURSOR`
       → algún redibujo/cambio de estado.
  - Esfuerzo: M. Bloquea: la "paridad ShaderGlass completa".
- [ ] **BUG: el menú (Ctrl+Alt+M) en DX12 ventana abre pero NO recibe clics**
  (no puedes pulsar nada ni cerrarlo).
  - Causa (diagnosticada): en el esquema invertido, con el menú abierto (no
    click-through), el `WM_NCHITTEST` de win_mode devuelve `HTCAPTION`/HT*BORDER
    para TODA la ventana (para arrastrar/redimensionar sin tecla) → al clicar el
    menú, Windows lo trata como arrastre de ventana, no llega a ImGui.
  - Fix (mañana): cuando `menu.is_open()`, el `WM_NCHITTEST` de win_mode debe
    devolver `HTCLIENT` (no HTCAPTION) para que ImGui reciba los clics. Hace
    falta un flag global (p.ej. `g_dx12_menu_open`) que lea el wndproc, o
    condicionar el bloque NCHITTEST de win_mode a `!menu_open`.
  - Esfuerzo: S. En `dx12_wndproc` WM_NCHITTEST (`src/overlay/overlay_mode_win.cpp`).
- [ ] (menor) Vídeo MPO en negro en DX12 (C-1, límite WGC). Vía real: GL+WGC
  capture. Esfuerzo: L. Diferido.

## Reglas operativas nuevas
- (ya en skill `tubelight-overlay-renderer-debug`: P-5/P-6/P-7/CT-7). Al tocar el
  overlay, **leer esa skill primero** — lleva directo a la conclusión sin repetir
  el saga de ~19 commits.
- Para verificar lo VISUAL del overlay hace falta el usuario: headless no sirve
  (CAPTURABLE=1 auto-captura→feedback; sin capturable no se puede screenshot por
  WDA). Pedir confirmación visual al usuario, no inferir de screenshots.

## Estado al cerrar
- HEAD: `185114e` (pushed main). Tree limpio.
- Build: limpio (RTX 2080 Ti, Release). 0 errores validación D3D12.
- Default renderer: **DX12**. Modo ventana = técnica ShaderGlass.
- Funciona: CRT visible, bajo lag, click-through (Ctrl), resize sin vibrar.
- Bug abierto: jitter al mover el ratón (ver Tasks, hipótesis #1 = vsync).

## Próximo paso al retomar
Probar `set_vsync(true)` (Present(1)) en win_mode (backend_d3d12 ya tiene
`set_vsync`; quitar el `d12->set_vsync(false)` del bloque win_mode en
`run_dx12`, ~tras el ShowWindow) y que el usuario confirme si el jitter al mover
el ratón desaparece — es la hipótesis #1 (tearing por vsync off).

## Comandos para arrancar rápido próxima vez
```bash
cd /d/AgentWorkspace/Tubelight && git pull && git log --oneline -3
cat docs/sessions/SESSION_DEBRIEF_2026_05_30b_OVERLAY.md
# leer la skill: tubelight-overlay-renderer-debug (P-1..P-7, CT-1..CT-7, C-1)
# build: cmake --build build/windows-vcpkg --config Release --target tubelight
# probar: doble-clic tubelight.exe (raíz) -> DX12 ventana, mover el ratón
```
