# Spec — Tubelight

## TLDR

Aplicación standalone Windows/Linux que aplica un shader CRT de alta fidelidad como overlay sobre cualquier ventana o pantalla completa. Calidad de simulación superior a CRT-Royale / Guest-Advanced gracias a pipeline 8-pass con perfiles de dispositivo basados en números reales documentados y modelado de la cadena completa de señal (conexión → tubo → fósforo → vidrio). Latencia añadida objetivo `<2 ms` vía hook al swap chain (inyección estilo ReShade en Win, Vulkan layer + LD_PRELOAD en Linux).

## Problema

Los shaders CRT actuales (CRT-Royale, CRT-Geom, Lottes, Guest-Advanced) son artísticamente convincentes pero:

1. **Mezclan parámetros**: el "tipo de máscara" y el "tipo de fósforo" se controlan por sliders desligados del dispositivo histórico real. Un usuario quiere "PVM Trinitron", no "aperture grille pitch 0.4 + phosphor warm".
2. **No modelan la cadena de señal**: empiezan en el framebuffer RGB del juego, ignorando la degradación de RF/Composite/S-Video. El ejemplo canónico de las cascadas de Sonic (líneas verticales que sólo se funden por composite + bandwidth limit) NO funciona bien en estos shaders.
3. **No tienen efectos temporales reales**: persistencia de fósforo por canal, calentamiento, voltage blooming dinámico, interferencia magnética/térmica — ningún shader actual los implementa con rigor.
4. **Sólo viven dentro de emuladores (RetroArch)**: no hay overlay standalone que aplique este nivel de calidad sobre cualquier app/ventana del sistema.

Evidencia citada en `docs/research/SOURCES.md`. Comparativa visual de las cascadas Sonic en PVM real vs emulación: https://x.com/CRTpixels/status/1368235112387604481

## Usuarios y casos de uso

- **U1 — Aficionado retro con emulador no-libretro**: usa Mednafen, MAME standalone, BlastEm, etc. Quiere efecto CRT pero sus emuladores no exponen pipeline de shaders. Frecuencia: sesiones de varias horas, varias veces por semana.
- **U2 — Aficionado retro con RetroArch**: ya tiene shaders pero quiere mayor fidelidad o exportar el preset a `.slangp` para usarlo dentro de RetroArch.
- **U3 — Cinéfilo retro**: ve vídeos de YouTube / VLC / TV ripeada y quiere efecto CRT sobre el reproductor. No hace gaming.
- **U4 — Streamer / creador de contenido**: graba gameplay con OBS y quiere overlay CRT antes de la captura, no como post-proceso de edición.
- **U5 — Demoscener / artista**: quiere usar Tubelight como ventana de previsualización de un programa propio (executable, demo, shader externo).
- **U6 — Jugador de juegos nuevos con estética retro**: títulos modernos diseñados para evocar 8/16 bit (UFO 50, Sea of Stars, Shovel Knight, Pixel Cup Soccer, etc.). Quiere consumir esos juegos a través del filtro CRT del hardware que emulan estéticamente.
- **U7 — Jugador de juegos recompilados / port nativo**: consume Mario 64 PC port, Zelda OoT native port, juegos descompilados+recompilados a PC. Quiere efecto CRT para acercarse a la experiencia original sin necesidad de emular.
- **U8 — Espectador de vídeo retro**: reproductores VLC, mpv, MPC-HC, YouTube Desktop, navegador con vídeo embebido. Material grabado de VHS/Betamax, capturas de juegos, contenido analógico digitalizado.
- **U9 — Cualquier app del sistema**: caso meta — el usuario quiere aplicar el filtro CRT sobre cualquier ventana/proceso del sistema operativo sin restricción de género o tipo. Tubelight no asume nada sobre el contenido.

## Objetivos medibles

- **M1 — Latencia añadida (camino feliz)**: con inyección Present()/swap activa, latencia añadida `<2 ms` medida con AMD FLM o equivalente sobre vídeo 60 Hz de referencia. Valor inicial actual sin Tubelight: 0 ms.
- **M2 — Latencia añadida (fallback DXGI/PipeWire)**: `≤16.7 ms` (1 frame) — degradación honesta, no oculta.
- **M3 — Cobertura de máscaras**: ≥6 tipos seleccionables en runtime (Shadow Mask, Aperture Grille, Slot Mask, Diamond, CGWG-mix, Dot Trio). Cada una con parámetros (pitch, shape, depth) configurables.
- **M4 — Cobertura de perfiles de dispositivo**: ≥10 perfiles preconfigurados con números reales citados en su JSON (PVM-8220, PVM-20M2/M4, Commodore 1084S, Sharp X68000 CZ-602D, Sharp CZ-614D, Wells-Gardner K7000, NEC MultiSync I, Terminal P31 verde, Terminal P3 ámbar, TV B&W P4).
- **M5 — Cobertura de conexiones**: 7 tipos en SignalProfile (RF, Composite NTSC, Composite PAL, S-Video, SCART RGB, Component YPbPr, RGB/VGA).
- **M6 — FPS sostenido en GPU referencia**: 60 fps en 4K (3840×2160) sobre GPU consumer **mínima** RTX 2060 / RX 5700 / Intel Arc A580 o equivalente. Medido con `tests/perf/`.
- **M6b — Adaptabilidad a GPU**: preset auto-detect en arranque que elige `quality` / `balanced` / `performance` según GPU detectada. Funciona dignamente desde GTX 1050 / RX 560 (preset performance, 1080p 60fps) hasta GPUs de gama alta (preset quality, 4K 144fps).
- **M7 — Cross-platform paridad**: el mismo `profile.json` produce salida bit-identical (o tolerancia ε≤2/255 por canal) en Windows y Linux con la misma GPU. Verificado con golden frames.
- **M8 — Citas en perfiles**: 100% de los perfiles tienen campo `source: { url, page, retrieved_at }` por cada número físico (dot pitch, decay ms, bandwidth).

## No-objetivos

- **N1 — No es un emulador de hardware**: no emula CPU/PPU/APU de consolas; sólo simula el display. La fuente de imagen es lo que el usuario ya está corriendo.
- **N2 — Sin captura de audio**: v1 no procesa audio del sistema. El "sonido CRT" (transformador flyback, degaussing) queda diferido a v2.
- **N3 — Sin HDR en v1**: salida SDR (8 bit por canal) en v1. HDR10 queda diferido.
- **N4 — Sin driver kernel**: no se desarrolla driver WDDM (Win) ni KMS/DRM hook (Linux) en v1. Lo más bajo es Vulkan layer.
- **N5 — Sin AntiCheat bypass**: si un emulador o juego activa anti-cheat que rechaza la inyección, Tubelight cae a fallback DXGI/PipeWire — no se intenta evadir.
- **N6 — No es plugin RetroArch**: integración nativa con libretro queda fuera de v1. Sí se ofrece exportar preset a `.slangp` como utilidad.
- **N7 — Sin interacción con el contenido**: Tubelight muestra, no manipula. No reescala, no salva estados, no inyecta controles.

## Glosario

- **Pass**: una etapa de render con su propio shader y FBO de salida. El pipeline tiene 8 (Pass −1 a Pass 6).
- **CRTProfile**: estructura JSON que define un dispositivo histórico (PVM-8220, etc) con sus parámetros físicos.
- **SignalProfile**: estructura JSON que define la cadena de señal entre fuente y tubo (RF/Composite/RGB/etc).
- **Replaceable Part**: componente cuya implementación puede sustituirse sin tocar contratos (ver `DESIGN.md`).
- **Hook feliz**: inyección de DLL (Win) o LD_PRELOAD/Vulkan layer (Linux) que intercepta el swap del proceso target. Latencia <2 ms.
- **Fallback captura**: DXGI Desktop Duplication (Win) o PipeWire screencast portal (Linux). Latencia +1 frame.
- **PAR**: Pixel Aspect Ratio. Para sistemas retro raramente es 1:1 (ver `docs/research/SOURCES.md` §2).
- **TVL**: TV-lines — métrica de resolución horizontal efectiva en señal analógica.
- **Dot pitch**: distancia entre fósforos del mismo color en la máscara, medida en mm.
- **Gate**: criterio binario que debe pasar para cerrar una fase del PLAN.
