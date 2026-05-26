# Design — Tubelight

## Arquitectura general

```
┌─────────────────────────────────────────────────────────────────┐
│                    Tubelight UI (standalone)                    │
│                     [REPLACEABLE: Dear ImGui]                   │
│   - selector de perfil CRT, conexión, máscara                   │
│   - live preview ventana propia                                 │
│   - target picker (ventana / pantalla / proceso)                │
└───────────────────────┬─────────────────────────────────────────┘
                        │ IPC (named pipe Win / unix socket Linux)
                        │ JSON schema en CONTRACTS.md §C1
        ┌───────────────┴────────────────────────────┐
        │              tubelight-core                │
        │  [CORE — pipeline orquestador]             │
        │  - GLSL → glslang → SPIR-V → SPIRV-Cross → │
        │    HLSL/GLSL backend específico            │
        │  - load perfil JSON                        │
        │  - pipeline 8-pass                         │
        └─────────────┬─────────────────┬────────────┘
                      │                 │
        ┌─────────────▼──────┐  ┌───────▼────────────┐
        │  injection (Win)   │  │  preload (Linux)   │
        │  [REPLACEABLE x4]  │  │  [REPLACEABLE x3]  │
        │  - DX11 hook       │  │  - LD_PRELOAD      │
        │  - DX12 hook       │  │    glXSwapBuffers  │
        │  - OpenGL hook     │  │    eglSwapBuffers  │
        │  - Vulkan layer    │  │  - Vulkan layer    │
        └─────────────┬──────┘  └───────┬────────────┘
                      │                 │
        ┌─────────────▼─────────────────▼────────────┐
        │           capture-fallback                  │
        │   [REPLACEABLE: DXGI / PipeWire]            │
        │   - usado cuando hook falla / no aplicable  │
        │   - latencia +1 frame                       │
        └─────────────────────────────────────────────┘
```

3 procesos en runtime:
1. **UI standalone** — proceso propio del usuario, siempre visible.
2. **Backend** — DLL/SO inyectado en el proceso target. Lleva el pipeline.
3. **Target** — el emulador/juego/reproductor (no toca Tubelight, no sabe que existe).

Comunicación UI ↔ Backend vía IPC (pipe nombrado en Win, Unix socket en Linux). El backend reporta status (hooked OK / fallback / error) y la UI envía cambios de perfil/máscara/conexión en caliente.

## Componentes

| Componente | Path en repo | Responsabilidad | Reemplazable |
|---|---|---|---|
| tubelight-ui | `src/ui/` | Panel de control, preview, target picker | `[REPLACEABLE: ImGui→Qt]` |
| tubelight-core | `src/core/` | Orquestador del pipeline, carga de perfiles, IPC server | `[CORE]` |
| shaders | `shaders/*.glsl` | 8 archivos GLSL del pipeline | `[REPLACEABLE: cada pass independientemente]` |
| profiles | `profiles/*.json` | CRTProfile + SignalProfile con números citados | `[REPLACEABLE: por perfil]` |
| injection-win | `src/platform/win/inject/` | Inyector + 4 hooks (DX11/12/OpenGL/Vulkan) | `[REPLACEABLE x4]` |
| preload-linux | `src/platform/linux/preload/` | `LD_PRELOAD` para glX/EGL + Vulkan layer | `[REPLACEABLE x3]` |
| capture-fallback | `src/capture/` | DXGI Desktop Duplication (Win) + PipeWire (Linux) | `[REPLACEABLE]` |
| ipc | `src/ipc/` | Cliente y servidor IPC con schema JSON | `[REPLACEABLE: pipe→gRPC]` |
| cli | `src/cli/` | Modo headless / scripting | `[OPTIONAL]` |

## Decisiones técnicas con evidencia

### D1 — Pipeline 8-pass (incluye Pass −1 de señal)
**Decisión**: el pipeline tiene 8 etapas:

```
Pass −1 → Modelado de señal (RF/Composite/SVideo/SCART/Component/VGA)
          BW limit luma+chroma, dot crawl, ringing, ghosting, noise
Pass  0 → Análisis: detección dithering + luminancia media del frame
Pass  1 → Reconstrucción dithering (fusión A/B en espacio nativo)
Pass  2 → Beam spreading: Gaussian intensidad-dependiente + scanlines físicas
          + linearización gamma CRT (≈2.5)
Pass  3 → Shadow mask 3D: textura + normal map + paralaje + especular
Pass  4 → Bloom multiescala + halación cromática (R/G/B radios distintos)
Pass  5 → Temporal: persistencia fósforo por canal (R/G/B decay distinto)
          + voltage bloom (frame-mean dependent) + BFI opcional
Pass  6 → Composición: barrel distortion, vignetting, convergencia,
          interferencia magnética/térmica, ruido térmico, gamma display
```

**Alternativas descartadas**:
- A: pipeline 3-pass tipo CRT-Geom (rápido pero pierde dithering reconstruction y voltage bloom).
- B: pipeline 5-pass tipo CRT-Royale (mejor que A, sin Pass −1 de señal — el efecto Sonic no se simula bien).
**Evidencia**: `docs/research/SOURCES.md` §6 (shaders existentes) + §8 (efectos dependientes de cadena de señal). Cascada Sonic requiere modelado de bandwidth NTSC composite (~0.5 MHz chroma efectivo) que ningún shader actual hace explícitamente.

### D2 — GLSL único + SPIRV-Cross
**Decisión**: shaders escritos en GLSL Vulkan dialect. Build pipeline:
```
*.glsl → glslang → *.spv → SPIRV-Cross → *.hlsl (D3D11/12)
                       └→ SPIRV-Cross → *.glsl GL (legacy OpenGL si necesario)
```
**Alternativas descartadas**:
- A: escribir HLSL para Win y GLSL para Linux. Doble mantenimiento.
- B: usar slang shaders (formato RetroArch). Vendor-specific, peor tooling.
**Evidencia**: SPIRV-Cross es la herramienta canónica de Khronos (https://github.com/KhronosGroup/SPIRV-Cross), licencia Apache 2.0, soportada por Khronos directamente.

### D3 — Inyección DLL en Win, LD_PRELOAD + Vulkan layer en Linux
**Decisión**: hook al `Present()` (D3D11/12) / `wglSwapBuffers` (OpenGL) / `vkQueuePresentKHR` (Vulkan) en el proceso target.
**Alternativas descartadas**:
- A: DXGI Desktop Duplication only. Lag de 1 frame inaceptable como camino feliz.
- B: driver WDDM kernel mode. Latencia 0, complejidad de driver Windows firmado — fuera de scope v1.
**Evidencia**: ReShade implementa exactamente este patrón con éxito en miles de juegos: https://github.com/crosire/reshade/blob/c0a9237c6a32e4e2166c3ed38c0fdf5979b8172f/source/d3d11/d3d11.cpp. Vulkan layer Khronos spec: https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderLayerInterface.md.

### D4 — Linux tiene paridad de ruta limpia
**Decisión**: en Linux, OpenGL se hookea con `LD_PRELOAD` + `dlsym(RTLD_NEXT, ...)` — característica oficial del SO, no hack. Vulkan via layer manifest JSON.
**Alternativas descartadas**:
- A: emular el patrón Windows en Linux. Innecesariamente complejo.
**Evidencia**: glx_hook (https://github.com/derhass/glx_hook) y libstrangle son referencias públicas de la técnica.

### D5 — Captura fallback con anuncio explícito
**Decisión**: si la inyección falla (anti-cheat, proceso 32-bit con backend 64-bit, error de hook), Tubelight cae a DXGI Desktop Duplication (Win) o PipeWire screencast portal (Linux). La UI muestra warning "modo captura — +1 frame de latencia".
**Alternativas descartadas**:
- A: fallar y pedir al usuario que cierre/abra el target. Mala UX.
- B: ocultar el lag. Deshonesto (viola C5).
**Evidencia**: AMD FLM (https://gpuopen.com/flm/) documenta overhead típico de DXGI; usuarios cualificados pueden tomar la decisión informada.

### D6 — Perfiles JSON versionables con citas embebidas
**Decisión**: cada perfil es un JSON con campo `source` por cada número físico. Ejemplo en CONTRACTS.md §C3.
**Alternativas descartadas**:
- A: hardcoded en C++ (sería una constitución violada — C2 imposible de auditar).
- B: SQLite (sobrekill — son <100 perfiles).
**Evidencia**: convención usada por shadertoy presets, CRT-Guest-Advanced LUTs.

### D7 — Build system CMake + vcpkg (Win) / system pkgs (Linux)
**Decisión**: CMake como build system único. En Win se gestionan deps con vcpkg manifiesto (`vcpkg.json`). En Linux con paquetes del distro (apt/dnf/pacman) listados en `docs/USER_GUIDE.md`.
**Alternativas descartadas**:
- A: Meson — menos tooling Windows.
- B: Bazel — sobrekill para repo C++ mediana.
**Evidencia**: vcpkg manifiesto está estable desde 2020 y CMake 3.20+ lo integra nativamente.

## Flujo de datos

```
Usuario lanza tubelight-ui.exe
    │
    ├─→ UI carga profiles/ + lista procesos del sistema
    │
Usuario selecciona target (ej: mednafen.exe) y profile (ej: pvm-8220.json)
    │
    ├─→ UI escribe IPC: "attach <pid> profile=pvm-8220"
    │
tubelight-core spawnea inyector
    │
    ├─→ inyector resuelve API target (DX11? DX12? GL? Vulkan?) y carga backend.dll
    │
backend.dll dentro del proceso target
    │
    ├─→ hookea Present() correspondiente
    │
Cada frame que el target presenta:
    │
    ├─→ backbuffer → Pass −1 → Pass 0 → ... → Pass 6 → SwapChain
    │
    └─→ status report a UI por IPC (fps, lag medido, errores)
```

Si en cualquier paso la inyección falla:
```
    └─→ fallback: lanzar capture process con DXGI/PipeWire
        + overlay ventana propia con shader aplicado
        + warning UI "+1 frame de latencia"
```

## Estado y persistencia

- `profiles/*.json` — perfiles preinstalados, versionados en repo.
- `~/.config/tubelight/profiles/*.json` (Linux) / `%APPDATA%\Tubelight\profiles\*.json` (Win) — perfiles de usuario.
- `~/.config/tubelight/settings.json` — preferencias UI (último target, último profile, posición ventana).
- Sin base de datos. Todo en JSON plano editable a mano.

Logs:
- `~/.local/share/tubelight/logs/tubelight.log` (Linux) / `%LOCALAPPDATA%\Tubelight\logs\tubelight.log` (Win)
- rotated por tamaño, 10 MB × 5 archivos.
