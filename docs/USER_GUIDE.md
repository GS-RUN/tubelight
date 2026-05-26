# Tubelight — User Guide

> Cross-platform CRT overlay. Estado: **pre-alpha**. Esta guía cubre build + run en Windows y Linux. Sin paths personales: todo relativo a la repo o variables de entorno.

## Requisitos comunes

- GPU compatible OpenGL 4.5 / Vulkan 1.2 / DirectX 11 (cualquier GPU consumer 2016+).
- 64 bits únicamente (x86_64 / amd64). ARM64 sin soporte oficial en v1.
- 4 GB RAM mínimo. 8 GB recomendado si el target consume mucha VRAM.
- Pantalla SDR. HDR queda diferido a v2.

---

## Build en Windows

### Toolchain

| Componente | Versión mínima | Notas |
|---|---|---|
| Visual Studio 2022 | 17.8+ | Workload "Desktop development with C++" |
| CMake | 3.26+ | Viene con VS o `winget install Kitware.CMake` |
| Git | 2.40+ | `winget install Git.Git` |
| vcpkg | (incluido como submódulo) | `git submodule update --init` |

### Dependencias (gestionadas por vcpkg manifest)

Listadas en `vcpkg.json` raíz. Se instalan automáticamente al hacer `cmake -B build`:

- `glfw3` — ventana + input
- `glm` — matemática vectorial
- `glslang` — compilador GLSL → SPIR-V
- `spirv-cross` — SPIR-V → HLSL/GLSL backend
- `nlohmann-json` — parser JSON
- `dear-imgui` — UI panel de control
- `minhook` — function hooking para inyección DX/GL
- `stb` — image loader (header-only via vcpkg)

### Pasos

```powershell
git clone <repo-url> tubelight
cd tubelight
git submodule update --init --recursive
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="vcpkg\scripts\buildsystems\vcpkg.cmake"
cmake --build build --config Release
```

El ejecutable queda en `build\Release\tubelight.exe`.

### Run

```powershell
build\Release\tubelight.exe                                  # UI standalone, sin target
build\Release\tubelight.exe --target retroarch.exe           # adjuntar a RetroArch
build\Release\tubelight.exe --profile pvm-8220 --signal composite_ntsc
build\Release\tubelight.exe --help                           # todos los flags
```

### Firma de código y SmartScreen

En v1 el binario **no está firmado**. Al ejecutar por primera vez Windows SmartScreen advertirá. Click en "Más información" → "Ejecutar de todas formas". Una vez aceptado no vuelve a preguntar.

Si tu antivirus pone el ejecutable en cuarentena (algunos AV reaccionan agresivo a `MinHook` por la inyección), añade la carpeta de build a la lista de exclusiones del AV.

---

## Build en Linux

### Toolchain

| Componente | Versión mínima | Notas |
|---|---|---|
| gcc | 12+ | o clang 16+ |
| CMake | 3.26+ | |
| Git | 2.40+ | |
| pkg-config | cualquiera | |

### Dependencias del sistema

#### Ubuntu 24.04 / Debian 12+

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake git pkg-config ninja-build \
  libglfw3-dev libglm-dev libepoxy-dev libstb-dev \
  libvulkan-dev vulkan-validationlayers vulkan-tools \
  glslang-tools spirv-tools libspirv-cross-c-shared-dev \
  nlohmann-json3-dev \
  libwayland-dev libxkbcommon-dev \
  libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
  libpipewire-0.3-dev \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev
```

#### Fedora 40+

```bash
sudo dnf install -y \
  gcc-c++ cmake git pkgconf ninja-build \
  glfw-devel glm-devel libepoxy-devel stb_image-devel \
  vulkan-loader-devel vulkan-validation-layers vulkan-tools \
  glslang spirv-tools spirv-cross-devel \
  json-devel \
  wayland-devel libxkbcommon-devel \
  libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel \
  pipewire-devel \
  ffmpeg-devel
```

#### Arch / Manjaro

```bash
sudo pacman -S --needed \
  base-devel cmake git pkgconf ninja \
  glfw glm libepoxy stb \
  vulkan-headers vulkan-validation-layers vulkan-tools \
  glslang spirv-tools spirv-cross \
  nlohmann-json \
  wayland libxkbcommon \
  libxrandr libxinerama libxcursor libxi \
  pipewire \
  ffmpeg
```

#### openSUSE Tumbleweed

```bash
sudo zypper install -t pattern devel_C_C++
sudo zypper install -y \
  cmake git pkgconf ninja \
  glfw-devel glm-devel libepoxy-devel stb-devel \
  vulkan-devel vulkan-validationlayers vulkan-tools \
  glslang-devel spirv-tools spirv-cross-devel \
  nlohmann_json-devel \
  wayland-devel libxkbcommon-devel \
  libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel \
  pipewire-devel \
  ffmpeg-7-libavcodec-devel ffmpeg-7-libavformat-devel
```

### Pasos

```bash
git clone <repo-url> tubelight
cd tubelight
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

El ejecutable queda en `build/tubelight`. Las librerías inyectables en `build/libtubelight_preload.so` y `build/libVkLayer_tubelight.so`.

### Run

#### Modo UI standalone

```bash
./build/tubelight
```

#### Inyección LD_PRELOAD sobre OpenGL (X11 o XWayland)

```bash
LD_PRELOAD=$(realpath build/libtubelight_preload.so) mednafen ROM.nes
```

Para activarlo automáticamente con un emulador concreto crea un wrapper:

```bash
#!/bin/bash
# ~/.local/bin/mednafen-crt
export LD_PRELOAD="$HOME/.local/lib/tubelight/libtubelight_preload.so"
export TUBELIGHT_PROFILE=pvm-8220
export TUBELIGHT_SIGNAL=composite_ntsc
exec mednafen "$@"
```

#### Vulkan layer (Win+Linux idéntico)

```bash
export VK_ADD_LAYER_PATH=$(realpath build)
export VK_INSTANCE_LAYERS=VK_LAYER_tubelight_overlay
rpcs3 # o cualquier app Vulkan
```

La variable `VK_ADD_LAYER_PATH` añade el directorio a las ubicaciones donde el loader busca manifests `.json`. No requiere root.

#### Wayland EGL

En sesión Wayland nativa, OpenGL apps usan EGL en lugar de GLX. Tubelight detecta automáticamente y hookea `eglSwapBuffers`. Mismo `LD_PRELOAD`, distinto símbolo interceptado.

#### Fallback PipeWire (cuando hook no aplica)

Cuando la inyección falla (o `--fallback always`), Tubelight pide acceso de screencast vía `xdg-desktop-portal`. Tu compositor mostrará un diálogo de permisos. Selecciona "Compartir esta ventana" o "Compartir pantalla completa".

Para evitar el prompt en cada sesión, GNOME 41+ y KDE soportan permisos persistentes — la opción está en el propio diálogo del portal.

---

## Configuración

### Perfiles preinstalados

Tubelight viene con perfiles JSON en `profiles/crts/` y `profiles/signals/`. Cada perfil tiene su `source` documentado. Los disponibles en v1 (ver SPEC.md §M4-M5):

**CRTs**: pvm-8220, pvm-20m4, commodore-1084s, sharp-x68k-cz602d, sharp-cz-614d, wells-gardner-k7000, nec-multisync-1, terminal-p31, terminal-p3, tv-bw-p4.

**Señales**: rf, composite_ntsc, composite_pal, svideo, scart_rgb, component, rgb_vga.

### Perfiles de usuario

Para crear perfiles propios, copia uno existente al directorio del usuario:

- Linux: `~/.config/tubelight/profiles/`
- Windows: `%APPDATA%\Tubelight\profiles\`

Edita el JSON. Valida con:

```bash
tubelight --validate-profile ~/.config/tubelight/profiles/mi-perfil.json
```

Detalle del formato en [`PRESET_AUTHORING.md`](PRESET_AUTHORING.md) (pendiente, F3).

### Variables de entorno

| Variable | Efecto |
|---|---|
| `TUBELIGHT_PROFILE` | profile_id por defecto al arrancar |
| `TUBELIGHT_SIGNAL` | signal_id por defecto |
| `TUBELIGHT_LOG_LEVEL` | `trace` / `debug` / `info` / `warn` / `error` |
| `TUBELIGHT_CONFIG_DIR` | sobrescribe ubicación de config (default XDG / APPDATA) |
| `VK_ADD_LAYER_PATH` | Vulkan loader (estándar Khronos) |
| `VK_INSTANCE_LAYERS` | activar layer `VK_LAYER_tubelight_overlay` |

---

## Troubleshooting

### "Hook failed" en Windows

Causas habituales:
1. Anti-cheat del target (no es bug de Tubelight; usa fallback DXGI con `--fallback always`).
2. Arquitectura mismatch: target 32-bit pero backend 64-bit. Solución pendiente (build 32-bit en v2).
3. DLL del wrapper colocada en path incorrecto: para OpenGL clásico la `opengl32.dll` wrapper va junto al `.exe` del target, no en `system32`.

### LD_PRELOAD no hookea en Linux

Verifica que el `.so` exporta el símbolo:
```bash
nm -D build/libtubelight_preload.so | grep glXSwapBuffers
```
Si está ausente, hay un fallo de build. Revisa CMake output.

### Vulkan layer no aparece

```bash
vulkaninfo | grep tubelight
```
Si no aparece, verifica `VK_ADD_LAYER_PATH` apunta al directorio donde está el `.json` manifest, no donde está el `.so`. El manifest contiene la ruta relativa al `.so`.

### Logs

- Linux: `~/.local/share/tubelight/logs/tubelight.log`
- Windows: `%LOCALAPPDATA%\Tubelight\logs\tubelight.log`

Rotated por tamaño (10 MB × 5 archivos).

---

## Desinstalación

Build manual: borrar la carpeta de la repo.
Installer Windows (v1.0): "Add/Remove Programs" → Tubelight → Uninstall.
AppImage Linux: borrar el archivo `.AppImage`.
Flatpak: `flatpak uninstall com.github.tubelight.Tubelight`.

Datos de usuario (perfiles + settings) sobreviven a la desinstalación. Para borrarlos:

- Linux: `rm -rf ~/.config/tubelight ~/.local/share/tubelight`
- Windows: borrar `%APPDATA%\Tubelight` y `%LOCALAPPDATA%\Tubelight`

---

## Más documentación

- [`SOURCES.md`](research/SOURCES.md) — fuentes técnicas citadas (fósforos, monitores, señales).
- [`specs/INDEX.md`](../specs/INDEX.md) — índice de la especificación completa.
- [`specs/PLAN.md`](../specs/PLAN.md) — roadmap por fases.
- [`specs/SPEC.md`](../specs/SPEC.md) — qué hace y qué NO hace Tubelight.
