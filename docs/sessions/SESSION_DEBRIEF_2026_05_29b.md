# Session Debrief — 2026-05-29 (b) — Tubelight

## TL;DR (3 líneas)
- **Intent**: arreglar el click-through del overlay DX12 (Path B).
- **Reality**: click-through RESUELTO; pero el síntoma final del usuario
  ("imagen congelada") resultó ser un bug DISTINTO — **WGC deja de entregar
  frames tras 1-2** en el path DX12. Fix temporal: el launcher usa **renderer GL**
  (captura viva + CT, verificados).
- **Próximo paso al retomar**: arreglar la **captura continua de WGC** en
  `src/capture/wgc_capture.cpp` para reactivar el camino DX12 zero-copy.

## Intent vs Reality
- **Intent**: dado que `WS_EX_NOREDIRECTIONBITMAP+TRANSPARENT` no pasaba clicks,
  implementar Path B (ventana layered) para click-through cross-process en DX12.
- **Reality**: Path B implementado y el click-through **funciona** (probado por
  el log del propio usuario: el foreground cambia al clicar a través del
  overlay). El usuario seguía diciendo "no funciona" por **tres causas
  encadenadas que NO eran el click-through**:
  1. Lanzaba en **modo ventana** (doble-clic = default GL windowed) → CT OFF por
     diseño (ADR-0001).
  2. **Instancias zombi** apiladas (topmost, robaban los clics).
  3. El **.bat lanzador que le di estaba malformado** (LF + paréntesis en REM →
     cmd escupía errores).
  Y, ya resuelto todo eso, el síntoma real: **imagen CONGELADA** = WGC freeze.
- **Divergencia (lo más valioso)**: pasamos toda la sesión "arreglando el
  click-through" cuando el click-through ya funcionaba a mitad de sesión; el
  bloqueo real era la **captura** (WGC) y la **operativa de lanzamiento**. La
  instrumentación (log a fichero del wndproc + WindowFromPoint + frame_count)
  fue lo que por fin separó "el clic no pasa" de "la imagen no se actualiza".

## Qué se hizo (8 commits, +577/-54 en 9 archivos, `92937de`..`4e02438`)
- `0d4f455` diag: auditoría click-through + probes EXSTYLE en runtime.
- `e0d2dee` feat: Path B — backend `BackendInitParams::layered` (composition
  swap chain sin árbol DComp) + overlay presenta por ULW.
- `f0584a4` fix: **ULW → SLWA+BitBlt** (ULW hit-testea por alpha y se tragaba
  los clics de píxeles opacos; SLWA hit-testea por rectángulo → CT real).
- `63186d1` diag: logger a fichero `tubelight_clickthrough.log` + trazado de
  mensajes de ratón en `dx12_wndproc` + WindowFromPoint periódico.
- `fdbf03d` chore: gate del log tras `TUBELIGHT_CT_LOG=1`.
- `2234868`/`59be65b` launcher .bat (creado mal con LF → reescrito CRLF+ASCII).
- `4e02438` fix: launcher → **renderer GL** (captura viva) + WGC wrapper
  mejorado (CreateFreeThreaded + Close de frames) aunque insuficiente solo;
  diagnóstico §8-9.

## Lecciones aprendidas (no obvias)
1. **Click-through cross-process exige ventana layered.** `WS_EX_TRANSPARENT`
   en ventana no-layered (NOREDIRECTIONBITMAP/DComp) NO pasa clicks;
   `WM_NCHITTEST→HTTRANSPARENT` es same-thread-only. D3D flip-model/DComp
   bypassea la superficie de redirección, así que para layered hay que presentar
   por **GDI** (BitBlt/ULW con readback).
2. **SLWA vs ULW para click-through**: con `UpdateLayeredWindow` el SO
   hit-testea contra el **alpha del bitmap** → píxeles opacos (que necesitas
   para ver) capturan el clic aunque haya `WS_EX_TRANSPARENT`. Con
   `SetLayeredWindowAttributes(LWA_ALPHA)` el hit-test es por **rectángulo** y
   `WS_EX_TRANSPARENT` sí pasa. **Usar SLWA**, no ULW, para overlays
   click-through visibles.
3. **`WindowFromPoint` ignora ventanas `WS_EX_TRANSPARENT`** → NO sirve para
   probar pass-through (siempre devuelve la de debajo). El probe fiable es: ¿el
   `wndproc` recibe `WM_LBUTTONDOWN`? y ¿cambia el foreground al clicar?
4. **WGC continuous-capture estaba roto y nunca se había detectado** porque solo
   `--wgc-test` (1 frame para PNG) se había validado. `FrameArrived` se dispara
   1-2 veces y para; ni Create→CreateFreeThreaded, ni Close de frames, ni poll
   en hilo principal lo resolvieron. **El path DX12+WGC NUNCA ha capturado en
   vivo de forma continua.**
5. **Los .bat de Windows exigen CRLF**; escritos con LF (Write tool / herramientas
   Unix) cmd los malinterpreta carácter a carácter. Escribirlos con
   `Set-Content -Encoding Ascii` (PS 5.1 = CRLF) y sin paréntesis en `REM`.
6. **El POST_BUILD que copia el exe a la raíz falla (MSB3073)** si hay un
   `tubelight.exe` corriendo O el **Explorador abierto en la carpeta Tubelight**
   (bloquean el destino). Matar procesos + cerrar Explorer antes de buildear.
7. **El exe de la raíz es lo que usa el usuario** (doble-clic) — verificar
   `Get-FileHash root == built` tras cada build; el default del doble-clic es
   **GL windowed** (sin CT).

## Decisiones tomadas
| Decisión | Alternativas descartadas | Trade-off | Revisar |
|---|---|---|---|
| Path B = layered + SLWA + BitBlt | ULW (traga clics); NOREDIRECTIONBITMAP+DComp (no CT) | 1 readback/frame en present | si se arregla WGC |
| Launcher usa renderer **GL** | DX12 (WGC congelado) | pierde zero-copy de captura | cuando WGC continuo funcione |
| Instrumentación gated `TUBELIGHT_CT_LOG=1` | siempre-on (ruido) | hay que activarla a mano | — |
| WGC wrapper: CreateFreeThreaded + Close frames | dejar Create + leak (original) | no suficiente solo, pero correcto | mañana, parte del fix |

## Tasks diferidos → TodoHub
- [ ] **P0 — Arreglar captura continua WGC (DX12 zero-copy).** Es EL bloqueo de
  mañana.
  - **Qué falta**: que `WgcCapture` entregue frames de forma continua. Hoy
    `frame_count` se clava en 1-2 (medido con `TUBELIGHT_CT_LOG=1`).
  - **Por qué se difirió**: problema de fondo WGC+D3D11On12; varios intentos
    fallidos; mejor atacarlo fresco con plan.
  - **Bloquea**: el camino DX12 (zero-copy, ~1000× menos coste de captura que
    GL). GL funciona como sustituto mientras tanto.
  - **Esfuerzo**: M-L.
  - **Plan de ataque (ver sección dedicada abajo)**.
- [ ] **P2 — i18n pass 2** (~70 cadenas del menú) — pendiente de antes, no tocado.
- [ ] **P3 — Menú DX12: vídeo MP4 + HUD** (necesitan readback/HUD path) — diferido.

## Reglas operativas nuevas (candidatas a CLAUDE.md)
- Escribir `.bat`/`.cmd` SIEMPRE con CRLF + ASCII (no LF).
- Antes de `cmake --build` en Windows: matar `tubelight.exe` y cerrar
  Explorer en la carpeta del proyecto (POST_BUILD a la raíz se bloquea).
- Para depurar interacción de ratón del overlay: `TUBELIGHT_CT_LOG=1` →
  `tubelight_clickthrough.log` junto al exe (consola oculta en doble-clic).

## Estado al cerrar
| Aspecto | Estado |
|---|---|
| HEAD | `4e02438` (pushed main) |
| Branch | main |
| Uncommitted | no (tree limpio) |
| Build | verde (RTX 2080 Ti, MSVC 19.44) |
| Overlay GL fullscreen | ✅ captura viva (2035 pts cambian) + click-through |
| Overlay DX12 fullscreen | ⚠️ click-through OK pero **captura congelada (WGC)** |
| Launcher | `Overlay-ClickThrough.bat` → GL |
| Blocker P0 | WGC continuous-capture freeze (DX12) |

---

## 🎯 PLAN DE ATAQUE MAÑANA — WGC continuous-capture freeze

### Síntoma exacto (reproducible)
```bat
set TUBELIGHT_CT_LOG=1
tubelight.exe --overlay-fullscreen --renderer dx12
:: ver tubelight_clickthrough.log → líneas "WGC: frame_count=N"
:: N se clava en 1-2 y NO crece (debería crecer ~60/s).
```
La imagen del overlay se congela en el primer frame capturado. Ocurre en
fullscreen Y en ventana (descartada oclusión y "nada cambia": se reprodujo con
el ratón moviéndose y pantalla cambiando).

### Qué se probó y NO funcionó (no repetir a ciegas)
1. `Direct3D11CaptureFramePool::Create` → `CreateFreeThreaded`: frame_count
   1→2. Ayudó algo pero sigue parando.
2. Cerrar el frame anterior (`held_frame.Close()`) al llegar uno nuevo para
   reciclar buffers: sigue clavado en 2 (luego NO es solo agotamiento de
   buffers — había buffer libre y aun así no produce).
3. Sondeo `TryGetNextFrame()` en el hilo principal sin callback: frame_count=1
   (sugiere que el pool necesita el callback FrameArrived registrado para
   producir; sin él solo sale el inicial).

### Hipótesis ordenadas para mañana
1. **No estamos liberando el `Surface`/frame correctamente y el pool deja de
   producir pese a haber buffer.** WGC requiere que la `Direct3D11CaptureFrame`
   (y su Surface) se liberen del todo. Hoy mantenemos `latest_tex` (la surface
   del pool) viva indefinidamente para que el pipeline la muestree el frame
   siguiente → eso fija un buffer. **Fix probable: copiar el frame a una textura
   PROPIA y liberar el frame del pool inmediatamente** (no retener buffers del
   pool nunca). Requiere:
   - `ID3D11Multithread::SetMultithreadProtected(TRUE)` en el contexto inmediato
     del device D3D11On12 (el callback corre en thread del pool → CopyResource
     concurrente con el hilo principal).
   - Double-buffer de la textura propia (front/back) + swap bajo mutex para que
     el hilo principal lea una estable mientras el callback escribe la otra.
   - La textura propia, creada en el device 11On12, sigue siendo unwrappable a
     D3D12 (igual que la surface WGC hoy).
2. **D3D11On12 + WGC tienen un problema de sincronización/flush** que para la
   producción. Revisar si `wrap_d3d11_texture`/`UnwrapUnderlyingResource` deja
   el device 11On12 en un estado que bloquea al pool. Probar un `Flush` del
   contexto D3D11 tras consumir.
3. **El tamaño del pool / formato no casa** y WGC produce 1 y luego falla
   silenciosamente. Encender el debug layer + drenar excepciones del callback
   (hoy el callback no loguea errores). Añadir try/catch con log dentro de
   `on_frame_arrived`.
4. **Camino alternativo robusto (si WGC sigue resistiéndose)**: usar **DXGI
   Desktop Duplication** para el path DX12 (como el GL), con exclusión vía
   Magnification API o `WDA`. Es lo que YA funciona en GL; portarlo a DX12
   garantiza captura viva, a costa de un copy/readback (pierde parte del
   zero-copy pero sigue siendo el pipeline DX12).

### Herramientas ya en su sitio
- Instrumentación `WGC: frame_count / new_textures_seen / tex11_now` en
  `overlay_mode_win.cpp` (gated `TUBELIGHT_CT_LOG=1`). Úsala para medir cada
  intento: el éxito = `frame_count` creciendo ~al refresh.
- `TUBELIGHT_D3D12_DEBUG=1` enciende el debug layer D3D12.
- Skill **`dx12-engineer` mode=debug** + **`native-debugger`** para el bug C++.
- Referencia OSS: el sample oficial de WGC (SimpleCapture, C++/WinRT) procesa el
  frame DENTRO del callback y lo libera — no retiene la surface. Confirmar ese
  patrón.

### Comando para arrancar rápido mañana
```bash
cd D:/AgentWorkspace/Tubelight
git pull && git log --oneline -8
cat docs/sessions/SESSION_DEBRIEF_2026_05_29b.md            # este doc
cat docs/sessions/DIAGNOSIS_CLICKTHROUGH_DX12_2026_05_29.md # §9 = el WGC freeze
# Reproducir y medir:
#   set TUBELIGHT_CT_LOG=1 && tubelight.exe --overlay-fullscreen --renderer dx12
#   → log "WGC: frame_count" debe quedarse clavado (estado actual).
# Atacar con hipótesis #1 (copiar a textura propia + ID3D11Multithread).
# Archivos: src/capture/wgc_capture.cpp (Impl::on_frame_arrived, start, latest_frame)
#           src/render/backend_d3d12.cpp (wrap_d3d11_texture, d3d11_on12_device)
```

### Próximo paso al retomar (una frase)
Implementar en `WgcCapture::Impl::on_frame_arrived` la **copia del frame a una
textura propia double-buffered (ID3D11Multithread protegido) + liberación
inmediata del frame del pool**, y verificar que `WGC: frame_count` crece de
forma continua con `TUBELIGHT_CT_LOG=1`.
