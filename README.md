# Tubelight

> High-fidelity CRT overlay for Windows and Linux. Aplica un shader CRT de alta fidelidad sobre cualquier ventana, juego, emulador o reproductor de vídeo del sistema.

[![CI](https://github.com/gs-run/tubelight/actions/workflows/ci.yml/badge.svg)](https://github.com/gs-run/tubelight/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

**Estado**: pre-alpha (fase F1 del PLAN). No usable todavía.

## Qué hace

Tubelight es una aplicación standalone que aplica un pipeline de shader CRT de 8 pasadas sobre el contenido renderizado por otra aplicación del sistema. A diferencia de los shaders CRT existentes (CRT-Royale, CRT-Geom, CRT-Lottes, Guest-Advanced):

- **Modela toda la cadena de señal** (RF → Composite → SVideo → SCART RGB → Component → RGB) antes del tubo — incluyendo los artefactos que hacían que efectos como las cascadas de Sonic en CRT se vieran como agua y no como rayas.
- **Perfiles de dispositivos históricos reales** con números físicos citados desde sus manuales de servicio (PVM-8220, Commodore 1084S, Sharp X68000, NEC MultiSync I, terminal P31 verde, TV B&W con P4, etc).
- **Efectos temporales reales**: persistencia de fósforo distinta por canal R/G/B, voltage blooming dependiente del frame, calentamiento progresivo, interferencia magnética/térmica.
- **Latencia mínima**: hook al swap chain del proceso target (estilo ReShade en Windows, Vulkan layer + `LD_PRELOAD` en Linux). Objetivo `<2 ms` añadidos. Fallback DXGI/PipeWire (+1 frame) cuando la inyección no es posible.

## Casos de uso

Cualquier aplicación que se quiera ver con estética CRT auténtica:

- Emuladores no-libretro (Mednafen, MAME standalone, BlastEm, …)
- Emuladores libretro / RetroArch (con export de preset a `.slangp`)
- Juegos nuevos con estética retro (UFO 50, Sea of Stars, Shovel Knight, Pixel Cup Soccer, …)
- Juegos descompilados / port nativo (Mario 64 PC port, Zelda OoT native, …)
- Reproductores de vídeo (VLC, mpv, MPC-HC, navegador, YouTube Desktop, …)
- Demos, ventana de previsualización de shaders externos, captura OBS pre-encoding

## Documentación

| Documento | Propósito |
|---|---|
| [docs/USER_GUIDE.md](docs/USER_GUIDE.md) | Build + run en Windows y Linux con dependencias por distro |
| [specs/INDEX.md](specs/INDEX.md) | Índice de la especificación completa (`spec-forge`) |
| [specs/SPEC.LOCKED.md](specs/SPEC.LOCKED.md) | Qué construye Tubelight, métricas, no-objetivos |
| [specs/PLAN.LOCKED.md](specs/PLAN.LOCKED.md) | Roadmap por fases con gates binarios |
| [specs/DESIGN.md](specs/DESIGN.md) | Arquitectura, pipeline 8-pass, decisiones |
| [specs/CONTRACTS.md](specs/CONTRACTS.md) | IPC, JSON Schema de perfiles, CLI |
| [docs/research/SOURCES.md](docs/research/SOURCES.md) | Fuentes técnicas citadas |

## Build rápido

```bash
# Linux (Ubuntu 24.04 / Fedora 40 / Arch — ver USER_GUIDE para todas las distros)
sudo apt install -y build-essential cmake ninja-build libglfw3-dev
cmake --preset linux-ninja
cmake --build build/linux-ninja
./build/linux-ninja/tubelight
```

```powershell
# Windows (PowerShell con vcpkg integrado)
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
cmake --preset windows-vcpkg
cmake --build build/windows-vcpkg --config Release
.\build\windows-vcpkg\Release\tubelight.exe
```

## Licencia

[MIT](LICENSE) © 2026 GS-RUN.

## Contribuir

Tubelight acepta contribuciones desde la fase F3 (perfiles de dispositivo). Cualquier perfil JSON propuesto via PR debe cumplir la regla **C2** (Constitution): cada número físico (dot pitch, decay de fósforo, bandwidth) lleva su `source.url` citado y verificable. Más detalles en [docs/PRESET_AUTHORING.md](docs/PRESET_AUTHORING.md) (disponible desde F3).
