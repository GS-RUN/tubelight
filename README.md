# Tubelight

> High-fidelity CRT overlay para Windows. Aplica un shader CRT de alta fidelidad sobre cualquier ventana, juego, emulador o reproductor de vídeo del sistema.
>
> **Linux próximamente** (v1.1) — Vulkan layer + PipeWire fallback ya están en árbol pero el build CI está en obras (jobs marcados `experimental`).

[![CI](https://github.com/gs-run/tubelight/actions/workflows/ci.yml/badge.svg)](https://github.com/gs-run/tubelight/actions/workflows/ci.yml)
[![License: PolyForm Noncommercial 1.0.0](https://img.shields.io/badge/license-PolyForm%20NC%201.0.0-blue)](LICENSE)

**Estado**: v0.1.3 publicado — overlay Windows completo (4 modos, 16 perfiles CRT + 7 signal, menú in-app, capture PNG/MP4, manual ES/EN). Descarga: [Releases](https://github.com/GS-RUN/tubelight/releases).

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

### Windows (soportado, v0.1.3)

```powershell
# PowerShell con vcpkg integrado
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
cmake --preset windows-vcpkg
cmake --build build/windows-vcpkg --config Release
.\build\windows-vcpkg\Release\tubelight.exe
```

O usa el helper: `scripts\build_windows.bat`.

### Linux — *próximamente* (v1.1)

El árbol incluye Vulkan layer + `LD_PRELOAD` + fallback PipeWire, pero
el build de CI aún no está verde (paquetes `vulkan-headers` /
`libpipewire-0.3-dev` y `find_package(glm)` con clang requieren ajustes).
Los jobs `Linux / gcc-13 (experimental)` y `Linux / clang-18
(experimental)` están marcados `continue-on-error` mientras se itera.

Si quieres trastear localmente:

```bash
sudo apt install -y build-essential cmake ninja-build \
    libglfw3-dev libglm-dev libepoxy-dev libstb-dev nlohmann-json3-dev \
    libvulkan-dev vulkan-headers libpipewire-0.3-dev
cmake --preset linux-ninja
cmake --build build/linux-ninja
./build/linux-ninja/tubelight
```

Issues / PRs bienvenidos para empujar v1.1 a verde.

## Licencia

[PolyForm Noncommercial 1.0.0](LICENSE) © 2026 Alonso J. Núñez (GS·RUN).

Gratis para cualquier uso no comercial (personal, investigación, educación, hobby, asociaciones sin ánimo de lucro). Para licencias comerciales: `gsrun.editor@gmail.com`.

## Contribuir

Tubelight acepta contribuciones desde la fase F3 (perfiles de dispositivo). Cualquier perfil JSON propuesto via PR debe cumplir la regla **C2** (Constitution): cada número físico (dot pitch, decay de fósforo, bandwidth) lleva su `source.url` citado y verificable. Más detalles en [docs/PRESET_AUTHORING.md](docs/PRESET_AUTHORING.md) (disponible desde F3).
