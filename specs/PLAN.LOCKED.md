# Plan — Tubelight

> 7 fases con gates binarios. C8 obliga a no cerrar fase sin gate verde.

## F1 — Esqueleto cross-platform y CI

**Objetivo**: repo compilable Win+Linux con ejecutable vacío que abre ventana GLFW.
**Tiempo estimado**: 1 semana
**Dependencias**: ninguna
**Entregable**:
- `CMakeLists.txt` + `vcpkg.json` (Win) + `docs/USER_GUIDE.md` con deps Linux
- `src/main.cpp` que abre ventana GLFW 800×600 y la cierra con ESC
- CI GitHub Actions con job Windows (MSVC) y Linux (gcc + clang)
- `.gitignore`, `LICENSE` (MIT propuesto), `README.md`
**Criterio de salida (gate)**:
- [ ] CI verde en Win + Linux desde commit limpio
- [ ] `cmake --build` sin warnings -Wall -Wextra
- [ ] `tubelight` ejecutable corre y cierra en ambos OS
- [ ] Cero paths absolutos en repo (grep verifica)
**Decisión post-gate**: continúa a F2.

## F2 — Pipeline shader puro (sin overlay todavía)

**Objetivo**: el pipeline 8-pass funciona sobre una imagen estática y un vídeo `.mp4` de prueba en ventana propia.
**Tiempo estimado**: 3 semanas
**Dependencias**: F1
**Entregable**:
- 8 archivos `shaders/passN.glsl` con la versión mínima de cada pass (pueden ser identity en muchos)
- `src/core/pipeline.cpp` orquestando los 8 passes con FBOs
- Loader de imagen (`stb_image`) y vídeo (`libav` opcional o `.png` secuencial)
- Pass 2 (scanlines+beam) + Pass 3 (shadow mask) + Pass 4 (bloom) implementados con calidad
- Modo `--shader-only <input.png>` para tests visuales
**Criterio de salida (gate)**:
- [ ] Side-by-side comparativa visual contra CRT-Royale sobre la misma imagen NES — Tubelight no peor en quality assessment (revisión manual con golden screenshots en `tests/golden/`)
- [ ] M6 parcial: 60 fps a 1080p con todos los passes activos en GPU referencia
- [ ] Cada pass desactivable individualmente (I4)
**Decisión post-gate**: continúa a F3.

## F3 — Perfiles de dispositivo con datos reales

**Objetivo**: 10 perfiles JSON cargables + UI mínima para conmutar entre ellos.
**Tiempo estimado**: 2 semanas (paralelo con F2 a partir de semana 2)
**Dependencias**: F2 al 50%
**Entregable**:
- `schemas/crt_profile.schema.json` + `schemas/signal_profile.schema.json` (JSON Schema)
- 10 CRTProfile: pvm-8220, pvm-20m4, commodore-1084s, sharp-x68k-cz602d, sharp-cz-614d, wells-gardner-k7000, nec-multisync-1, terminal-p31, terminal-p3, tv-bw-p4
- 7 SignalProfile: rf, composite_ntsc, composite_pal, svideo, scart_rgb, component, rgb_vga
- Cada uno con `source.url` citado en `docs/research/SOURCES.md` o pendiente marcado
- Validador `tubelight --validate-profile <path>`
**Criterio de salida (gate)**:
- [ ] M4 cumplido: ≥10 perfiles cargables
- [ ] M8 cumplido: 100% de perfiles con `source` en cada bloque de números físicos
- [ ] M5 cumplido: 7 SignalProfile
- [ ] `--validate-profile` rechaza JSON con número sin cita
**Decisión post-gate**: continúa a F4.

## F4 — Modelado de señal (Pass −1)

**Objetivo**: Pass −1 implementado con calidad — efecto Sonic cascadas reconocible al elegir composite NTSC.
**Tiempo estimado**: 2 semanas
**Dependencias**: F3
**Entregable**:
- `shaders/pass_minus1_signal.glsl` con BW limit luma+chroma, dot crawl, ringing, ghosting RF, ruido por línea
- Selector de SignalProfile en UI con preview en caliente
- Reconstrucción de dithering en Pass 0/1 (detecta tablero A/B y mezcla)
**Criterio de salida (gate)**:
- [ ] Test visual: Sonic frame de Green Hill cascada → composite_ntsc → cascada se ve como agua, no como rayas (revisión humana + golden screenshot)
- [ ] Test visual: misma imagen sin Pass −1 vs con Pass −1 → diff perceptual ≥ threshold (medible con SSIM en `tests/`)
- [ ] Validación cruzada con captura real de PVM si está disponible (opcional)
**Decisión post-gate**: continúa a F5.

## F5 — Inyección Windows (hook Present + LD_PRELOAD Linux OpenGL)

**Objetivo**: backend.dll/so se inyecta en proceso target real (RetroArch o mednafen como conejillo) y aplica el pipeline con `<2 ms` añadidos.
**Tiempo estimado**: 4 semanas
**Dependencias**: F2 (pipeline) + F3 (perfiles)
**Entregable**:
- `src/platform/win/inject/dx11_hook.cpp` con MinHook sobre `IDXGISwapChain::Present`
- `src/platform/win/inject/opengl_hook.cpp` con DLL wrapping de `opengl32.dll`
- `src/platform/linux/preload/glx_hook.cpp` con `LD_PRELOAD` sobre `glXSwapBuffers`
- `src/ipc/` con named pipe Win y Unix socket Linux
- UI muestra fps + lag medido en tiempo real
**Criterio de salida (gate)**:
- [ ] M1 cumplido: <2 ms medido con AMD FLM o tooling equivalente sobre vídeo 60 Hz
- [ ] Hook estable sobre RetroArch DX11 en Win durante sesión de 1 hora sin crash
- [ ] Hook estable sobre mednafen GL en Linux durante sesión de 1 hora sin crash
- [ ] Fallback DXGI funciona cuando inyección desactivada manualmente
**Decisión post-gate**: continúa a F6.

## F6 — Vulkan layer + DX12 + Wayland

**Objetivo**: cobertura API completa.
**Tiempo estimado**: 3 semanas
**Dependencias**: F5
**Entregable**:
- `src/platform/common/vulkan_layer/` con manifest JSON + hook `vkQueuePresentKHR` (sirve para Win y Linux)
- `src/platform/win/inject/dx12_hook.cpp`
- `src/platform/linux/preload/egl_hook.cpp` (Wayland/EGL)
- PipeWire screencast fallback Linux (`src/capture/pipewire.cpp`)
**Criterio de salida (gate)**:
- [ ] Vulkan layer probada con RPCS3 o Dolphin en Win+Linux
- [ ] DX12 probado con RetroArch DX12 driver
- [ ] EGL probado con un compositor Wayland (Sway o KWin)
- [ ] M7 paridad Win/Linux: golden frames del mismo perfil tienen ε≤2/255 entre OSes
**Decisión post-gate**: continúa a F7.

## F7 — Pulido, perfiles extra, export .slangp, release

**Objetivo**: v1.0 lista para distribución.
**Tiempo estimado**: 2 semanas
**Dependencias**: F6
**Entregable**:
- Perfiles adicionales (BVM-20F1, X68k CZ-603D, NEC MultiSync 4, FW900 si encontramos números)
- Efectos finales: voltage bloom, interferencia magnética, warm-up animation
- Exporter a `.slangp` (`--export-slangp <path>`)
- Installer Windows (NSIS o WiX, sin firma de código en v1)
- Empaquetado Linux: AppImage + Flatpak manifest
- `docs/USER_GUIDE.md` completo + `docs/PRESET_AUTHORING.md` para que la comunidad cree perfiles
- README con screenshots y comparativa contra CRT-Royale/Guest-Advanced
**Criterio de salida (gate)**:
- [ ] M2/M3/M4/M5/M6/M7/M8 todos cumplidos
- [ ] Installer Windows clean install + uninstall en VM limpia
- [ ] AppImage corre en Ubuntu 24.04 LTS + Fedora 41 sin instalar deps extra
- [ ] Demo de la cascada de Sonic funciona out-of-the-box con perfil "Mega Drive composite NTSC"
**Decisión post-gate**: release v1.0; v2 abre planning con HDR, audio CRT, captura ALSA/WASAPI.
