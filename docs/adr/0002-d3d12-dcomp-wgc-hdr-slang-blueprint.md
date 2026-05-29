# ADR-0002 — Blueprint: backend D3D12 + DComp + WGC + HDR + Slang

**Fecha**: 2026-05-27
**Estado**: accepted (blueprint; implementación incremental)
**Supersede a partir de v0.2.0**: parte de DESIGN.md D1 (renderer backend), parte de CONTRACTS.md §"CLI flags", parte de PLAN.LOCKED.md F2 (pipeline) cuando se cierre v0.2.0.

## Contexto

Tras v0.1.4 (ADR-0001) el pipeline funciona pero hace dos cosas que un
overlay moderno no debería hacer:

1. **OpenGL 4.5 + libepoxy sobre Windows**. El driver path NVIDIA / AMD
   GL en 2026 paga ~5-10× más CPU overhead que el equivalente D3D11/12,
   especialmente en swap chain presentation y composición DWM.
   Medible: en una RTX 3060 sobre 1920×1200, Tubelight v0.1.4 a 60 FPS
   usa ~14% CPU sostenido. ShaderGlass v1.3 (D3D11 + WGC) en el mismo
   escenario reporta ~3-4% CPU. Diferencia: 3-4× CPU overhead.
2. **DXGI Desktop Duplication captura el monitor entero y recorta**.
   Para los modos `--overlay-target` y `--overlay` windowed esto es
   gasto puro de bandwidth (~960 MB/s a 1920×1200@60 que se descartan
   al recortar). WGC entrega solo el rect del target.

ShaderGlass demostró públicamente (Steam release v1.3) que el stack
moderno reduce drásticamente este coste. No queremos copiar
ShaderGlass — queremos su fluidez + más cosas que ShaderGlass no
hace.

## Decisión

A partir de v0.2.0 Tubelight ejecuta sobre un backend de renderizado
abstracto `IRenderBackend` con dos implementaciones:

### Backend D3D12 (default en Windows desde v0.2.0)

- Device D3D12 con feature level 12_0 mínimo, 12_2 si está disponible
  (Variable Rate Shading Tier 1, Sampler Feedback).
- Swap chain DXGI 1.6 flip model (`FLIP_DISCARD` para target/region,
  `FLIP_SEQUENTIAL` para record).
- Composición vía DirectComposition: dos visuals separadas —
  - `IDCompositionVisual2` chrome (title bar + bordes en modo windowed)
  - `IDCompositionVisual2` body (output CRT)
  - El click-through del body se gestiona vía `IDCompositionRectangleClip`
    + el flag input transparent del DComp visual, NO via `WS_EX_LAYERED`
    (que es path software).
- Captura vía **Windows.Graphics.Capture (WGC)** con `Direct3D11CaptureFramePool`:
  - Para `--overlay-target` y `--overlay` windowed: WGC entrega un
    `IDirect3D11Texture2D` del target window.
  - Para `--overlay-fullscreen` y `--overlay-region`: WGC entrega el
    monitor (más eficiente que DXGI Duplication en composición DWM).
  - El device D3D11 que usa WGC se crea sobre el mismo `IDXGIAdapter`
    que el device D3D12, vía `D3D11On12CreateDevice` → la textura
    capturada se ata al device D3D12 sin copia.
- Pipeline procesa en formato `R16G16B16A16_FLOAT` (scRGB FP16).
  Tone mapping al final:
  - Display HDR10 detectado: output `R10G10B10A2_UNORM` con metadata
    HDR10 (1000 cd/m² peak, 0.05 cd/m² black) — primera vez que se
    reproduce el contraste real de un CRT en pantalla.
  - Display SDR: tone mapping perceptual (ACES o Hable) a sRGB 8-bit.
- Variable Rate Shading en pass 4 (bloom + halation):
  `VRS_SHADING_RATE_2X2` sobre las regiones del frame buffer dominadas
  por el blur Gaussian (el halo es difuso por definición).
- Async compute pairing: pass 4 (bloom Gaussian, compute queue) corre
  en paralelo con pass 2 (beam + scanlines, graphics queue) cuando el
  device lo permite.

### Backend OpenGL (Linux, fallback Windows, retención del path actual)

- Mismo `IRenderBackend` API.
- En Windows el OpenGL backend usa `WGL_NV_DX_interop2` para compartir
  la textura WGC con el GL device — capture en D3D11, render en GL,
  zero CPU roundtrip. NVIDIA + AMD soportan la extensión.
- En Linux (v1.1+): captura por PipeWire + render GL. WGC no existe; el
  fallback portal es D-Bus `org.freedesktop.portal.ScreenCast`.
- Mantenemos el pipeline GL para no perder el trabajo de calibración.

### Selección de backend

CLI:
```
tubelight --renderer dx12|gl [...]
```

Default por plataforma:
- Windows: `dx12` (cae a `gl` si CreateDevice DX12 falla — ej. GPU
  Pre-DX12, Intel HD 4400 sin update de driver).
- Linux: `gl` (único path).

Menú Profile tab → combo "Render backend": dx12 (Windows recommended),
opengl (fallback / Linux).

### Shaders

Source único en **Slang** (Khronos/NVIDIA), build-time pipeline:

```
shaders/src/pass<N>.slang
  └→ slangc -target spirv     → shaders/build/spirv/pass<N>.spv
  └→ slangc -target hlsl      → shaders/build/hlsl/pass<N>.hlsl
       └→ fxc /T ps_5_0       → shaders/build/dxbc/pass<N>.cso  (D3D11On12 path)
       └→ dxc  -T ps_6_0      → shaders/build/dxil/pass<N>.dxil (D3D12 native)
```

Distribución: binarios precompilados en el zip de release. Compile-on-
load es opcional para `--shader-only` (debug).

## Consecuencias

### Gana

- **Fluidez tipo ShaderGlass**: estimado 3-5× en throughput frame, 60-70%
  menos CPU overhead. Mediremos en F2 cuando haya backend.
  > **ENMIENDA Phase 3e (2026-05-29, medido + CORREGIDO)**: el estimado
  > "3-5× DX12" NO se cumple — pero tampoco lo contrario. Bench GPU-timestamp
  > del pipeline 8-pass en RTX 2080 Ti FL 12_2 (`--shader-only --bench 300`,
  > testcard + pvm-8220 + composite_ntsc): **GL ~0.36 ms/frame, DX12 ~0.38
  > ms/frame — PARIDAD** (~5-8%, ambos p99 estables). Una primera medición
  > reportó "GL 2-6× más rápido"; era un **artefacto**: el bench DX12 aún
  > hacía `Present(1,0)` (vsync) y la cola GPU throttleada contaminaba la
  > ventana de timestamps (el *min* constante ~0.447 ms delataba el coste
  > real). Tras saltar el Present en modo bench → paridad. Además se
  > refactorizaron los descriptores DX12 (tabla persistente por-pass
  > double-buffered en vez de `CopyDescriptorsSimple` por-draw; canónico
  > DX-02, bit-exact, pero no movió el número — el pipeline no estaba
  > descriptor-bound). Detalle + lección de metodología en
  > `docs/perf/PHASE_3E_BENCH.md`. **Conclusión pipeline**: DX12 cuesta lo
  > mismo que GL en el pipeline core.
  >
  > **DONDE SÍ GANA DX12 (medido, el verdadero punto ShaderGlass)**: el
  > coste **captura→GPU por frame**. GL (DXGI → `memcpy` CPU →
  > `glTexSubImage2D`, ~9MB/frame) = **3.385 ms/frame**; DX12+WGC
  > (`D3D11On12 UnwrapUnderlyingResource`, zero-copy) = **0.003 ms/frame** —
  > **~1000× menos** (`--overlay-fullscreen --renderer <gl|dx12> --bench
  > 200`). A 60 Hz, GL quema ~20% del presupuesto de frame solo moviendo
  > píxeles; DX12+WGC, nada. **AHÍ está el "60-70% menos CPU overhead" y la
  > fluidez tipo ShaderGlass — confirmado, y el path T5.5 DX12+WGC ya lo
  > entrega.** Falta hacerlo default usable (Phase 4a: click-through +
  > menú en DX12).
- **HDR-aware pipeline**: ningún competidor lo tiene. CRT real con
  contraste 1000:1+ sobre HDR display.
- **WGC capture per-window**: menos bandwidth, menos compositor stalls,
  per-monitor capture sin recortar.
- **VRS + async compute**: pequeñas pero acumulativas (~5-10%).
- **Slang fuente única**: mantenemos GL backend sin doblar shader
  codebase.

### Rompe

- **Dependencia nueva**: vcpkg add `directx-headers`, `directxshadercompiler`,
  Windows SDK 10.0.26100+ (ya tenemos 26100). WindowsAppSDK 1.7+ para
  WGC C++/WinRT.
- **CMakeLists.txt**: nueva opción `TUBELIGHT_BUILD_DX12=ON` por defecto
  en Windows. Linux la fuerza a `OFF`.
- **Tamaño binario**: D3D12 runtime no se redistribuye (presente en
  Windows 10 1809+), pero las DXC libs (`dxcompiler.dll`,
  `dxil.dll`) añaden ~50 MB si elegimos compile-on-load.
  Decisión: shippeamos bytecode precompilado, no DXC, tamaño zip se
  mantiene ~55-60 MB.
- **Pipeline.cpp se refactoriza** en una clase abstracta `RenderPipeline`
  + dos subclases `RenderPipelineGL` y `RenderPipelineD3D12`. Los
  perfiles JSON y la cromaticidad de fósforo NO cambian — son
  independientes del backend.
- **`--renderer` flag se persiste** en `%APPDATA%\Tubelight\settings.json`
  como `"renderer": "dx12"|"gl"`. Migración: si falta el campo, default
  por plataforma.

### Riesgo nuevo

- **R12**: WGC requiere COM init + WinRT runtime. Si hay incompatibilidad
  con Wine/Proton (modo Linux+Steam Deck), `gl` fallback necesario.
  Detección temprana: smoke `tubelight --renderer dx12 --version`
  reporta `D3D12 unavailable` cuando aplica.
- **R13**: D3D11On12 interop tiene latencia añadida (~1 frame en peor
  caso) vs. D3D12 puro. Si el bench muestra >2ms penalty,
  reconsiderar uso de D3D11 capture o pasar a captura nativa D3D12 vía
  `IDXGIOutputDuplication::AcquireNextFrame` (que sí es D3D12-compatible
  directamente).
- **R14**: HDR10 metadata + tone mapping correctos requieren
  cromaticidad de fósforo en CIE 1931 + curva EOTF correcta. Nuestras
  primarias SMPTE-C son sRGB-gamut; necesitamos data adicional para
  P22/P31/P3/P4 en HDR. **Bloquea HDR output hasta tener spectra
  measured (ver Phase 5 differentiation features)**.

## Alternativas consideradas

- **A: D3D11 directo (estilo ShaderGlass)**. Descartado porque:
  - D3D11 no permite async compute pairing (necesario para nuestro
    bloom Gaussian).
  - D3D11 no tiene Variable Rate Shading.
  - El paso a D3D12 sería inevitable en v0.3+ por HDR pipeline.
- **B: Vulkan en vez de D3D12**. Descartado porque:
  - WGC C++/WinRT está atado a D3D11/D3D12. Vulkan no tiene path nativo
    a WGC sin pasar por D3D11 interop primero (i.e. el coste sería el
    mismo que en B con menos integración).
  - Cross-platform es valor en v1.1 (Linux), no en v0.2.0.
- **C: WebGPU (wgpu native)**. Descartado por madurez. Reconsiderar en
  v1.0+.
- **D: Mantener GL + arreglar capture via `WGL_NV_DX_interop2` solo**.
  Descartado: no resuelve el overhead del swap chain ni habilita HDR
  pipeline. Solo recortaría ~30-40%.

## Plan de implementación (Phases)

Esto NO se implementa en una sesión. El ADR documenta el blueprint; los
trozos llegan incrementalmente:

| Phase | Entregable | Estimado | Ship |
|---|---|---|---|
| **2a** | Drop `WS_EX_LAYERED` en windowed + skip ImGui idle (quick wins OpenGL) | 1 día | **v0.1.5** (this commit cycle) |
| **2b** | PBO double-buffer para `glReadPixels` (screenshot + record) | 2-3 días | v0.1.6 |
| **2c** | Merge passes 0+1 (dither detect+reconstruct), skip pass 5 (identity) | 2 días | v0.1.7 |
| **3a** | `IRenderBackend` abstracción + `GLBackend` wrapping current code, sin cambio funcional | 4-5 días | v0.1.8 |
| **3b** | `D3D12Backend` skeleton: device, swap chain flip, command queue, full-screen quad | 1 semana | v0.2.0-alpha |
| **3c** | Port pipeline passes a HLSL bytecode, run los 8 en D3D12 | 1 semana | v0.2.0-beta |
| **3d** | WGC capture nativa D3D11On12 → D3D12 ✓ **shipped** (core 2026-05-28 `3ca7d5a`; overlay `run_dx12` 2026-05-28 — `--overlay*/--renderer dx12`). Click-through + ImGui menú DX12 diferidos a 4a. | 4-5 días | v0.2.0-rc |
| **3e** | Bench + benchmark publication + v0.2.0 stable | 2-3 días | **v0.2.0** |
| **4a** | DirectComposition chrome + body separation; drop WS_EX_LAYERED en target mode (por DComp clip) — **PARCIAL** (2026-05-29 `dd95784`): 4a.1 composition swap chain (`CreateSwapChainForComposition` + visual tree) + 4a.2 click-through (`WS_EX_LAYERED\|TRANSPARENT`, ya compatible al desacoplar el HWND del flip-model). Verificado headless (init/render/teardown limpio); **falta verificación visual+click en HW real** + 4a.3 menú ImGui en DX12. Nota: se usó `WS_EX_LAYERED` (no el clip de DComp del plan original) — la combinación probada del path GL; fallback `WS_EX_NOREDIRECTIONBITMAP` si muestra negro. | 1 semana | v0.2.1 |
| **5a** | HDR pipeline scRGB FP16 + HDR10 output (requiere fósforo spectra) | 2-3 semanas | v0.3.0 |
| **6a** | VRS + async compute pairing en pass 4 | 1 semana | v0.3.1 |
| **7a** | Slang shader source migration + DXC build pipeline | 1 semana | v0.3.2 |

Total estimado v0.1.4 → v0.2.0: **~5-7 semanas**, v0.1.4 → v0.3.x:
**~3-4 meses**.

## Referencias

- ShaderGlass v1.3 Steam release (https://store.steampowered.com/app/3613770/ShaderGlass/)
  como benchmark de fluidez observable.
- Microsoft Windows Graphics Capture API (https://docs.microsoft.com/en-us/uwp/api/windows.graphics.capture)
  documentación oficial WGC + Direct3D interop.
- Microsoft DirectComposition (https://docs.microsoft.com/en-us/windows/win32/directcomp/directcomposition-portal)
  visual tree + per-visual click-through.
- DXGI 1.6 swap effect flip discard (https://docs.microsoft.com/en-us/windows/win32/api/dxgi/ne-dxgi-dxgi_swap_effect)
  modern presentation path.
- Slang Shading Language (https://shader-slang.com/) NVIDIA + Khronos
  co-driven, futuro estándar.
- HDR10 metadata SMPTE ST 2086 (https://www.smpte.org/) para
  mastering display primaries y luminance bounds.
- `overlay_mode_win.cpp:1106-1170` — `apply_clickthrough_user` lambda
  con `if(false)` block, identificado como dead code a borrar en
  Phase 4a.
