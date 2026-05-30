# Audit DX12 vs GL — modo ventana (2026-05-30)

Skill: `gpu-backend-playbook` mode=compare. Motivado por el reporte del usuario:
GL ventana perfecto (arrastre sin tecla + click-through + resize en vivo + capta
vídeo RTVE en directo); DX12 peor (vibra al resize, necesita Ctrl, **vídeo en
negro**).

## Arquitectura de cada backend

| Aspecto | GL | DX12 |
|---|---|---|
| Captura | **Magnification API** (primario, excluye el overlay con `MagSetWindowFilterList`) + DXGI Desktop Duplication (fallback) | **WGC** (Windows.Graphics.Capture) `CreateForMonitor` + D3D11On12 interop, zero-copy |
| Present ventana | Ventana normal (swap GL, compuesta por DWM) | Ventana `WS_EX_LAYERED` pintada por **BitBlt** del readback |
| Click-through | `WM_NCHITTEST → HTTRANSPARENT` en el cliente (sin layered) → arrastras por la barra de título, el cliente deja pasar clics | `WS_EX_TRANSPARENT` (todo-o-nada) → necesita Ctrl para alternar interactivo/click-through |

## Hallazgo 1 — Vídeo en negro (DX12) = limitación de WGC

- **WGC captura la composición de DWM** (formato B8G8R8A8). El vídeo en directo
  (acelerado por hardware) suele presentarse en un **plano de overlay
  multi-plano (MPO) / independent-flip** que NO está en la composición de DWM →
  WGC entrega negro en esa zona. Si el vídeo provoca un cambio de modo de
  presentación del monitor, la captura WGC entera puede quedar en negro
  ("todo en negro").
- **La Magnification API (GL) hace screen-scrape de la salida FINAL ya
  compuesta** (incluidos los planos de overlay) → capta el vídeo sin problema.
- **Conclusión**: es una limitación de fondo de WGC, no un bug puntual. La única
  forma de captar ese vídeo es la vía Mag/DXGI-Dup que usa GL — que es un camino
  de **readback por CPU**, justo lo contrario del zero-copy que justifica WGC en
  DX12. No hay arreglo de WGC que capte MPO sin abandonar el zero-copy.

## Hallazgo 2 — Vibración al resize (DX12) = present layered

- DX12 pinta por `WS_EX_LAYERED` + BitBlt. Windows realoca la superficie layered
  en cada paso de tamaño → hueco/parpadeo. GL pinta por ventana normal (DWM lo
  compone suave) → sin vibración.
- **Arreglable** adoptando el diseño de ventana de GL (ventana normal +
  `NCHITTEST→HTTRANSPARENT`, sin layered) → present por swap chain normal → resize
  suave. PERO no arregla el Hallazgo 1 (el vídeo seguiría en negro con WGC).

## Hallazgo 3 — UX de ventana

GL: ventana decorada (arrastras por la barra, sin tecla) + cliente click-through.
Mejor que el Ctrl-para-mover de DX12. Logrado sin layered, vía NCHITTEST.

## Conclusión y recomendación

Para **modo ventana sobre contenido real (incluido vídeo)**, **GL es el backend
correcto** — y arquitectónicamente, no por falta de pulido en DX12. El zero-copy
de DX12+WGC es valioso en **pantalla completa sobre contenido sin MPO**
(emulador), pero en ventana es peor y **no capta vídeo**.

**Recomendación**: modo ventana por defecto → **GL**. DX12 queda como
`--renderer dx12` para el nicho zero-copy (fullscreen/perf, sin vídeo).

Opcional (si se quiere mantener DX12 ventana para no-vídeo): portar el diseño de
ventana de GL (no-layered + NCHITTEST) a `run_dx12` para matar la vibración — pero
el vídeo seguiría en negro por WGC.
