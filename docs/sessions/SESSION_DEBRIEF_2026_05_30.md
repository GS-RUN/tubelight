# Session Debrief — 2026-05-30 — Tubelight

**HEAD `baae667`** (pushed main). Sesión muy larga: cerró el click-through DX12
end-to-end, arregló WGC, reconfiguró los defaults, y dejó EN MARCHA el rediseño
del modo ventana. Tree limpio. Los `.bat` lanzadores se BORRARON (el exe es
autosuficiente).

## TL;DR
- **Intent**: que el modo ventana sea una ventana nativa (título + min/max/
  cerrar, arrastrable) cuyo contenido sea un CRT **click-through** que filtra la
  **región de detrás**, en GL y DX12.
- **Reality**: hecho el cimiento (recorte por región en DX12 + defaults + toggle
  FS + guardar default). **PENDIENTE la pieza grande**: el rediseño de DOS
  VENTANAS (marco nativo + CRT click-through pegado).
- **Próximo paso**: implementar el FrameWindow nativo (chrome) + ventana CRT
  click-through que lo sigue. Empezar por DX12, luego GL. Ver §"PLAN" abajo.

## Qué se cerró esta sesión (commits clave)
- `ee8444e` **WGC continuous-capture fix**: WGC no recicla buffers sobre device
  D3D11On12 → corre en device plano dedicado + textura compartida (zero-copy).
  Captura DX12 ya va VIVA.
- `541c817`+`24fc4fc` **menú DX12 click-away**: abrir el menú quitaba TRANSPARENT
  y se quedaba pegado → clic fuera cierra el menú (usa `WantCaptureMouse`, NO el
  return de ImGui_ImplWin32_WndProcHandler que da 0 incluso en widgets).
- `3189efa` GL: mismo click-away (paridad).
- `82b08d3`+`8c67783` **switch GL↔DX12 en menú** vía relanzamiento limpio
  (hard-exit ExitProcess para no solapar ventanas).
- `cf59016` **default DX12 + preset "basic"** (`profiles/crts/basic.json`:
  aperture grille + escaneo suave; `apply_crt_profile` id=="basic" anula
  bloom/halación/barril/viñeta/persistencia y deja target_aspect=0). Señal
  default rgb_vga. (main.cpp: backend default D3D12 en Windows.)
- `e374e2a` **toggle ventana↔fullscreen** (Ctrl+Alt+Enter / botón menú) vía
  relanzamiento (`relaunch_with_fullscreen` añade/quita `--overlay-fullscreen`).
  **Borrados los .bat**.
- `955547a` **"Guardar como predeterminada"**: persiste perfil+señal+TODOS los
  params a `%APPDATA%\Tubelight\default_config.json`
  (`preset_saver::save/load_default_config`); arranque sin --profile/--signal
  (`Options::use_saved_default`) lo carga sobre el básico.
- `baae667` **recorte por región DX12**: `WgcCapture::set_crop(x,y,w,h)` recorta
  la copia WGC→shared con `CopySubresourceRegion`; run_dx12 windowed/region
  actualiza el crop cada frame desde el rect cliente de la ventana. **Arregla
  el "copia el escritorio entero"** — ahora muestra la región de detrás 1:1.

## Estado actual (verificado RTX 2080 Ti)
- **Doble-clic exe** → DX12 **ventana** + preset básico (rejilla limpia, sin
  estelas, sin tocar aspecto). Ventana **framed normal** (arrastrable por título)
  que muestra la región de detrás recortada. **SIN click-through todavía** en
  ventana (eso es la pieza pendiente).
- **Ctrl+Alt+Enter** → fullscreen con click-through (funciona). Otra vez → ventana.
- **Menú** (Ctrl+Alt+M): perfiles/señal/sliders + botones "Cambiar a GL/DX12",
  "Guardar como predeterminada", "Go fullscreen".
- GL y DX12 funcionan; click-through OK en fullscreen/target/region; menú robusto
  en ambos.

## ⛔ PENDIENTE — rediseño modo VENTANA (la pieza grande)

**Requisito del usuario** (literal): "ventana nativa de Windows (título,
minimizar, maximizar, cerrar, arrastrable y redimensionable) cuyo contenido es
un CRT click-through que filtra la región de detrás, en GL y DX12."

**Por qué no es trivial**: una ventana click-through (`WS_EX_LAYERED|TRANSPARENT`)
lo es en TODA su superficie → no recibe ratón ni para arrastrarse ni para la
barra de título. Imposible "ventana framed + cliente click-through" en una sola
ventana (WS_EX_TRANSPARENT es all-or-nothing). HTTRANSPARENT por WM_NCHITTEST es
same-thread-only (no cruza procesos).

### Diseño acordado: DOS VENTANAS
1. **FrameWindow** (chrome): ventana nativa `WS_OVERLAPPEDWINDOW` (título OS +
   minimizar/maximizar/cerrar + redimensionar + arrastrar). `WDA_EXCLUDE`
   FROMCAPTURE para que el CRT capture lo que hay DETRÁS de ella. Su área
   cliente queda vacía (la tapa el CRT).
2. **CRT window** (la ventana de render actual, GL o DX12): `WS_EX_LAYERED|
   TRANSPARENT|NOACTIVATE|TOOLWINDOW|TOPMOST`, borderless (WS_POPUP), posicionada
   EXACTAMENTE sobre el rect CLIENTE (screen coords) del FrameWindow. Click-through.
   Recortada a su región (ya existe `set_crop` en DX12; GL ya recorta por
   `upload_subregion`). `WDA_EXCLUDEFROMCAPTURE`.
3. **Sincronización**: el wndproc del FrameWindow:
   - `WM_MOVE`/`WM_WINDOWPOSCHANGED`/`WM_MOVING` → reposicionar la CRT window al
     rect cliente (sigue al arrastrar; durante el modal-drag el render se congela
     pero la posición sigue — opcional: WM_TIMER como el path GL).
   - `WM_SIZE` (SIZE_RESTORED) → resize CRT + señalar al overlay para
     `backend->resize` + `pipeline.resize` + actualizar crop size.
   - `WM_SYSCOMMAND SC_MINIMIZE` → `ShowWindow(crt, SW_HIDE)`; al restaurar,
     `SW_SHOWNOACTIVATE`.
   - `WM_SYSCOMMAND SC_MAXIMIZE` → opción: relanzar/ir a fullscreen, o
     redimensionar el frame al monitor (CRT sigue).
   - `WM_CLOSE` → quit.
   - z-order: CRT TOPMOST cubre SOLO el cliente (no la barra de título, que está
     encima del cliente) → barra visible y clickable, cliente tapado por el CRT.

### Dónde tocar
- **DX12** (`run_dx12` en `src/overlay/overlay_mode_win.cpp`): hoy crea UNA
  ventana (hwnd, raw Win32). Para windowed: crear el FrameWindow + hacer la hwnd
  actual la CRT window click-through (ya existe el path `layered`); bombear los
  mensajes de ambas (PeekMessage ya recorre todas las ventanas del hilo). El crop
  ya sigue el rect cliente — apuntar al rect cliente del FrameWindow.
- **GL** (`run`): hoy windowed es una ventana GLFW framed normal (ya recorta por
  `upload_subregion_to_source`). Convertir la GLFW window a borderless layered
  click-through (como el GL fullscreen, líneas ~1684) + crear el FrameWindow +
  sincronizar. El recorte ya sigue la posición (read_window_rect_on_monitor).
- **Empezar por DX12** (default), dejarlo entero, luego GL a la par.

### Riesgos / notas
- Durante el modal-drag del FrameWindow, el loop principal se bloquea → el CRT se
  congela mientras arrastras (el path GL lo resuelve con WM_TIMER + subclass; se
  puede portar). Aceptable v1, pulir después.
- Maximizar: decidir si redimensiona el frame al monitor (CRT sigue, sigue
  recortando = básicamente fullscreen pero con barra) o si va al fullscreen real.
- El FrameWindow excluido de captura (`WDA_EXCLUDEFROMCAPTURE`) es CLAVE para que
  el CRT vea la región de detrás y no su propio cliente.

## Cómo arrancar el próximo chat
```bash
cd D:/AgentWorkspace/Tubelight && git pull && git log --oneline -6
cat docs/sessions/SESSION_DEBRIEF_2026_05_30.md   # este doc
# Probar el estado actual:
#   doble-clic tubelight.exe (raiz) -> DX12 ventana basica, muestra region detras.
#   Ctrl+Alt+Enter -> fullscreen click-through.
# Implementar el FrameWindow (chrome nativo) + CRT window click-through, DX12 primero.
# Archivos: src/overlay/overlay_mode_win.cpp (run_dx12 + run), capture/wgc_capture.* (set_crop ya hecho).
```

## Lecciones de la sesión (no obvias)
1. **WGC no recicla buffers sobre device D3D11On12** → device plano dedicado +
   textura compartida (shared handle) para zero-copy cross-device.
2. **ImGui_ImplWin32_WndProcHandler devuelve 0 incluso en clics sobre widgets**
   → para "clic fuera del menú" usar `ImGui::GetIO().WantCaptureMouse`, no el
   return del handler.
3. **Relanzar (switch render / FS) debe hard-exit (ExitProcess)** el proceso
   viejo: el teardown lento (DXGI/WGC/Mag/GLFW) deja su ventana encima
   bloqueando ("copia congelada del escritorio").
4. **Verificar click-through con la ACCIÓN real** (selección de archivo /
   foreground), no con tests COM (`SelectedItems` mentía) ni "el overlay no
   captura" (daban falso-OK). El log del wndproc fue lo definitivo.
5. **Ventana click-through = WS_EX_TRANSPARENT all-or-nothing**; "framed + cliente
   click-through" requiere DOS ventanas.
6. **Modo ventana debe recortar a la región** (no escalar el monitor entero):
   GL via upload_subregion, DX12 via set_crop + CopySubresourceRegion.
7. `.bat` con CRLF+ASCII (LF rompe cmd). POST_BUILD a la raíz falla si hay
   tubelight corriendo o Explorer abierto en la carpeta.

## Próximo paso (una frase)
Implementar el **FrameWindow nativo (chrome) + ventana CRT click-through pegada a
su cliente** para el modo ventana, empezando por DX12 (`run_dx12`) y luego GL,
con sincronización move/size/min/max/close.
