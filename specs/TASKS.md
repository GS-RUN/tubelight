# Tasks — Tubelight

> Items granulares. Tamaños S (<1d) / M (1-3d) / L (3-7d). DoD binario.

## F1 — Esqueleto cross-platform

- [ ] T1.1 (S) — Crear `CMakeLists.txt` raíz con `cxx_std 20`, opciones `TUBELIGHT_BUILD_TESTS`, `TUBELIGHT_BUILD_INSTALLER` — DoD: `cmake -B build` no falla en Win+Linux.
- [ ] T1.2 (S) — Crear `vcpkg.json` con deps GLFW + glm + glslang + spirv-cross + nlohmann_json + MinHook (Win only) — DoD: `vcpkg install` en CI Win OK.
- [ ] T1.3 (S) — `src/main.cpp` abre ventana GLFW 800×600, GL 3.3 core, cierra con ESC — DoD: ejecutable Win y Linux corre.
- [ ] T1.4 (S) — `.github/workflows/ci.yml` con jobs Win (windows-2022 + MSVC) y Linux (ubuntu-24.04 + gcc-13, clang-18) — DoD: badge verde en README.
- [ ] T1.5 (S) — `docs/USER_GUIDE.md` v0.1 con build instructions Windows (vcpkg manifest) y Linux (apt/dnf/pacman lists) — DoD: dev nuevo con Ubuntu limpio puede `git clone && build && run` siguiendo el doc.
- [ ] T1.6 (S) — Pre-commit hook que `grep -rE '[A-Z]:\\Users|/home/[a-z]+/' src/ docs/ specs/` y bloquea si hay match — DoD: hook falla al intentar commitear `D:\AgentWorkspace`.
- [ ] T1.7 (S) — `LICENSE` MIT + cabecera SPDX en cada `.cpp/.h` — DoD: `find src -name "*.cpp" -exec grep -L SPDX {} +` vacío.

## F2 — Pipeline shader puro

- [ ] T2.1 (M) — Sistema de carga shader GLSL → glslang → SPIR-V → SPIRV-Cross → GLSL backend final — DoD: `pipeline.load("shaders/passN.glsl")` produce módulo compilado en runtime.
- [ ] T2.2 (M) — `src/core/pipeline.cpp` con orquestación 8 FBOs encadenados — DoD: identity pipeline (todos los passes pasan textura sin tocar) produce out == in en pixel-comparison.
- [ ] T2.3 (M) — Pass 2 (beam + scanlines): scanline_strength uniform, Gaussian beam width modulado por luminance, linearización gamma 2.5 — DoD: imagen NES Mario uniforme se ve con scanlines visibles ajustables.
- [ ] T2.4 (M) — Pass 3 (shadow mask): aperture grille + shadow mask + slot mask + diamond seleccionable por uniform `mask_type` — DoD: 4 capturas con cada mask son visualmente distintas y reconocibles.
- [ ] T2.5 (M) — Pass 4 (bloom + halation): blur separable horizontal/vertical + radios distintos R/G/B — DoD: bordes rojos sangran más que verdes en imagen test.
- [ ] T2.6 (S) — Loader `stb_image` para input PNG — DoD: `tubelight --shader-only test.png` muestra imagen procesada.
- [ ] T2.7 (S) — Loader vídeo: `--shader-only test.mp4` via libav o secuencia `frame_%04d.png` (decisión: libav si compila clean cross-platform, si no PNG sequence) — DoD: vídeo corre a su fps nativo con pipeline aplicado.
- [ ] T2.8 (S) — Toggle individual de cada pass via teclado 1..8 — DoD: visualmente verificable.
- [ ] T2.9 (M) — `tests/golden/` con 5 frames de referencia (NES, SNES, MD, PS1, arcade) procesados con perfil "generic-pvm" — DoD: golden screenshots commitidos + script de comparación SSIM.
- [ ] T2.10 (S) — Benchmark perf 1080p y 4K con `tests/perf/` — DoD: número fps reportado.

## F3 — Perfiles

- [ ] T3.1 (S) — `schemas/crt_profile.schema.json` (JSON Schema draft-07) — DoD: validador rechaza JSON sin `phosphor.decay_ms`.
- [ ] T3.2 (S) — `schemas/signal_profile.schema.json` — DoD: idem para SignalProfile.
- [ ] T3.3 (M) — 10 CRTProfile en `profiles/crts/` con `source` en cada bloque — DoD: M4 cumplido + M8 cumplido (validador hace pasar 10/10).
- [ ] T3.4 (S) — 7 SignalProfile en `profiles/signals/` — DoD: M5 cumplido.
- [ ] T3.5 (S) — `tubelight --validate-profile <path>` — DoD: códigos de salida correctos por C4.
- [ ] T3.6 (M) — UI mínima Dear ImGui: combo selector de profile + signal + sliders de scanline/mask_pitch — DoD: cambio en UI se refleja en preview <100 ms.
- [ ] T3.7 (S) — Documento `docs/PRESET_AUTHORING.md` para que terceros creen perfiles — DoD: contribuidor externo puede crear perfil válido siguiendo el doc.

## F4 — Pass −1 señal

- [ ] T4.1 (M) — `shaders/pass_minus1_signal.glsl` con BW limit (low-pass separable luma+chroma con cutoff configurable) — DoD: composite NTSC limita chroma a 0.5 MHz visible en captura FFT.
- [ ] T4.2 (M) — Dot crawl simulado por modulación de subportadora vs reloj de pixel — DoD: patrón ajedrez muestra dot crawl característico (revisión visual contra captura PVM real).
- [ ] T4.3 (S) — Ringing en transiciones de alto contraste (oscilación de Gibbs aproximada) — DoD: borde blanco/negro muestra overshoot.
- [ ] T4.4 (S) — Ghosting RF (señal duplicada con offset horizontal) — DoD: switch RF activa fantasma.
- [ ] T4.5 (S) — Ruido por línea (no por pixel) en RF — DoD: tile de ruido vertical visible.
- [ ] T4.6 (M) — Pass 0 análisis dithering: detección de patrón A/B con kernel 2×2 — DoD: imagen con dithering devuelve mask >50% true; imagen sin dithering devuelve mask <5% true.
- [ ] T4.7 (M) — Pass 1 reconstrucción dithering: mezcla (A+B)/2 donde mask=true — DoD: cascada Sonic Green Hill se ve como agua, no como rayas (revisión humana).
- [ ] T4.8 (S) — Test SSIM sobre golden frames pre/post Pass −1 — DoD: diff >threshold, número reportado.

## F5 — Inyección Win + LD_PRELOAD Linux GL

- [ ] T5.1 (L) — `src/platform/win/inject/injector.exe` con `CreateRemoteThread + LoadLibrary` — DoD: inyecta `backend.dll` en `notepad.exe` sin crash.
- [ ] T5.2 (L) — `src/platform/win/inject/dx11_hook.cpp` con MinHook hookea `IDXGISwapChain::Present` — DoD: prueba sobre RetroArch DX11 driver, frame counter visible.
- [ ] T5.3 (L) — `src/platform/win/inject/opengl_hook.cpp` con DLL wrapping `opengl32.dll` colocado junto al `.exe` target — DoD: prueba sobre mednafen GL, frame counter visible.
- [ ] T5.4 (L) — `src/platform/linux/preload/glx_hook.cpp` con `LD_PRELOAD` y `dlsym(RTLD_NEXT, "glXSwapBuffers")` — DoD: `LD_PRELOAD=./libtubelight.so mednafen ...` aplica shader.
- [ ] T5.5 (M) — `src/ipc/` con named pipe Win + Unix socket Linux, JSON line-delimited — DoD: UI envía `set_profile` → backend recibe < 5 ms latency.
- [ ] T5.6 (M) — Pipeline integrado en hook: dentro del Present(), aplicar 8 passes sobre backbuffer y presentar resultado — DoD: visualmente aplicado en RetroArch.
- [ ] T5.7 (M) — Métrica de latencia añadida con timestamps GPU (`glQueryCounter` / `ID3D11Query D3D11_QUERY_TIMESTAMP`) — DoD: número reportado a UI 1 vez/s.
- [ ] T5.8 (M) — Validación con AMD FLM externo — DoD: M1 cumplido (<2 ms).
- [ ] T5.9 (S) — Fallback DXGI Desktop Duplication cuando hook desactivado — DoD: switch UI conmuta y reporta `+1 frame`.

## F6 — Vulkan + DX12 + Wayland

- [ ] T6.1 (L) — `src/platform/common/vulkan_layer/` con manifest JSON `VkLayer_tubelight.json` + implementación de capa que hookea `vkQueuePresentKHR` — DoD: capa instalable vía `VK_LAYER_PATH` y aplica shader en RPCS3 o Dolphin Vulkan.
- [ ] T6.2 (L) — `src/platform/win/inject/dx12_hook.cpp` con MinHook sobre vtable `IDXGISwapChain3::Present` — DoD: prueba sobre target DX12.
- [ ] T6.3 (M) — `src/platform/linux/preload/egl_hook.cpp` con `eglSwapBuffers` — DoD: prueba sobre Wayland compositor.
- [ ] T6.4 (M) — `src/capture/pipewire.cpp` usando portal `org.freedesktop.portal.ScreenCast` — DoD: captura framebuffer en Wayland sin permisos root.
- [ ] T6.5 (M) — Golden frames cross-platform: ejecutar mismo perfil + input en Win y Linux con misma GPU; comparar — DoD: M7 cumplido (ε≤2/255).

## F7 — Pulido + release

- [ ] T7.1 (M) — 5 perfiles adicionales (BVM-20F1, CZ-603D, NEC MultiSync 4, FW900, perfil custom usuario X68k tri-sync) — DoD: validador OK + capturas demostrativas en `docs/screenshots/`.
- [ ] T7.2 (M) — Pass 5 voltage bloom: luminance frame-mean en buffer 1×1 → afecta beam width — DoD: pantalla blanca → beam más ancho visible.
- [ ] T7.3 (S) — Pass 6 interferencia magnética: `sin(time * 0.3 + y * 0.1) * 0.0003` distorsión — DoD: variación temporal observable, no estática.
- [ ] T7.4 (M) — Warm-up animation 0-180s al `attach` — DoD: primeros 3 minutos muestran color drift + brightness ramp.
- [ ] T7.5 (M) — `--export-slangp <path>` genera preset compatible con RetroArch — DoD: preset cargado en RetroArch produce visualmente lo mismo (ε≤4/255).
- [ ] T7.6 (M) — Installer Windows NSIS — DoD: instalación en VM Win11 limpia funciona; uninstall limpia.
- [ ] T7.7 (M) — AppImage Linux con `linuxdeploy` — DoD: corre en Ubuntu 24.04 + Fedora 41 sin deps extra del sistema.
- [ ] T7.8 (M) — Flatpak manifest — DoD: build `flatpak-builder` OK; instalación local OK.
- [ ] T7.9 (S) — README screenshots: side-by-side Tubelight vs CRT-Royale vs CRT-Guest-Advanced — DoD: 3 imágenes commiteadas + comentario técnico breve.
- [ ] T7.10 (S) — CHANGELOG.md desde v0.1.0-alpha hasta v1.0.0 — DoD: cada entry con commit hash.
