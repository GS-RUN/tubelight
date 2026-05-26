# Tubelight — Fuentes técnicas citadas

> Materia prima para SPEC y DESIGN. Cada dato lleva su URL. Huecos marcados como `NO ENCONTRADO`/`[NEEDS-MEASUREMENT]` para no inventar.

Fecha de recolección: 2026-05-26.

---

## 1. Fósforos JEDEC / EIA

El registro original de designaciones P-x lo mantenía la EIA (no JEDEC directamente). Los documentos primarios (EIA-RS-160 / RS-179 / JEDEC JEP116) no son públicos. Datos siguientes de fuentes secundarias.

| Fósforo | Color/λ | Decaimiento al 10% | Aplicación | Fuente |
|---|---|---|---|---|
| P1 | Verde 525 nm | 1–100 ms (mezclas comerciales) | Osciloscopios, monocromo | https://en.wikipedia.org/wiki/Phosphor |
| P3 | Ámbar 602 nm | ~13 ms | Terminales ámbar | https://en.wikipedia.org/wiki/Phosphor |
| P4 | Blanco azulado (componentes 565/540 nm) | Persistencia corta, decay exponencial + ley inversa | TV B&W | https://www.epanorama.net/documents/video/phosphor_decay.html |
| P22 azul | ZnS:Ag, ~450 nm | <100 µs | TV color (B) | https://www.epanorama.net/documents/video/phosphor_decay.html |
| P22 verde | ZnS:Cu,Al, ~530 nm | <100 µs | TV color (G) | idem |
| P22 rojo | Y₂O₂S:Eu, ~611 nm | "varias centenas de µs", variantes hasta ~1 ms | TV color (R) | idem |
| P31 | ZnS:Cu verde-amarillento | 0.01–1 ms; medición fotópica: 1.4% a 50 ms | Osciloscopios rápidos, VT100, Apple II | https://pubmed.ncbi.nlm.nih.gov/9176941/ |

Cromaticidad P22 (SMPTE-C): R x=0.63 y=0.34; G x=0.31 y=0.595 — https://crtdatabase.com/faq/phosphor-designations

**Huecos**: tabla EIA canónica completa NO ENCONTRADA online. Lab Guys World PDF (http://www.labguysworld.com/crt_phosphor_research.pdf) contiene la tabla pero requiere descarga manual.

---

## 2. Pixel Aspect Ratios reales

| Sistema | Resolución activa | PAR | Fuente |
|---|---|---|---|
| NES NTSC | 256×240 | **8:7** exacto | https://www.nesdev.org/wiki/Overscan |
| NES PAL | 256×240 | **≈1.3862** (18:13 con 0.11% error) | idem |
| SNES 256×224 | 256×224 | 8:7 (mismo PPU rate) | https://forums.nesdev.org/viewtopic.php?t=23885 |
| SNES hi-res 512×448 | 512×448 interlace | 4:7 lógico (8:7 a nivel "logical pixel") | https://forums.nesdev.org/viewtopic.php?t=11932 |
| Mega Drive H40 | 320×224 | **32:35** aprox | https://misterfpga.org/viewtopic.php?t=583 |
| Mega Drive H32 | 256×224 | 64:49 | idem |
| Master System | 256×192 | NO ENCONTRADO con cita primaria (comunidad asume 8:7 NTSC) | — |
| CPS2 | 384×224 | Pixel NO cuadrado, display 4:3, pixels más altos que anchos | https://shmups.system11.org/viewtopic.php?t=5661 |
| Neo Geo MVS | 320×224 (visible 304×224) | ≈28:25 | https://misterfpga.org/viewtopic.php?t=9425 |
| PlayStation | 256/320/384/512/640 × 224–480 | Varía por modo | https://emulation.gametechwiki.com/index.php/Resolution |
| Saturn | múltiples | 640×480=1:1; 320×240 pixel doblado | idem |
| N64 | 320×240 interno, doblado por VI | PAR efectivo ≈1:2 | https://polycount.com/discussion/226167 |

---

## 3. Monitores concretos

| Monitor | Máscara | Dot pitch | H freq | Tamaño | Fuente |
|---|---|---|---|---|---|
| Sony PVM-8220 | Aperture Grille (Trinitron) | 0.50 mm | 15 kHz | 8" | https://crtdatabase.com/crts/sony/sony-pvm-8220 |
| Sony PVM-9L2 | AG Trinitron | 0.50 mm | — | 9" (8" vis.) | https://crtdatabase.com/crts/sony/sony-pvm-9l2 |
| Sony PVM-2030 | AG | 0.55 mm | — | 20" | https://www.manualslib.com/manual/1296653/Sony-Pvm-2030.html?page=16 |
| Sony PVM-2530 | AG | 0.73 mm | — | 25" | manualslib |
| Sony PVM-2950QM | AG | 0.70–0.85 mm | — | 29" | https://www.manualslib.com/manual/1758617/Sony-Trinitron-Pvm-2950qm.html |
| Sony PVM-3230 | AG | 0.90 mm | — | 32" | https://www.manualslib.com/manual/1261396/Sony-Pvm-3230.html?page=29 |
| Sony BVM-20F1U | AG broadcast | 0.30 mm | hasta ~64 kHz | 20" | https://www.manualslib.com/manual/1037089/Sony-Trinitron-Bvm-A20f1u.html?page=124 |
| Commodore 1084S | Shadow Mask slotted | 0.42 mm | 15625–15750 Hz | 14" | https://www.manualslib.com/manual/1100625/Commodore-1084s.html?page=12 |
| Sharp X68000 CZ-600D/601D/602D | Shadow Mask | 0.39 mm | tri-sync 15/24/31 kHz | 14–15" | https://gamesx.com/wiki/doku.php?id=x68000%3Ax68000_monitors |
| Sharp X68000 CZ-603D/606D/608D | Shadow Mask | 0.28–0.31 mm | 15/31 kHz | 14–15" | idem |
| Sharp CZ-614D | Shadow Mask matte | 0.31 mm | 31 kHz | 14" | https://crtdatabase.com/crts/sharp/sharp-cz-614d |
| Wells-Gardner K7000 | Color raster | NO ENCONTRADO (manual PDF lo trae) | 15.72 kHz | 13"/19" | https://wellsgardner.com/wp-content/uploads/2016/08/K7000-1.pdf |
| Electrohome G07 | Color raster | NO ENCONTRADO (manual servicio Randy Fromm 1982) | 15.75 kHz | 13"/19" | https://archive.org/details/ArcadeGameManualG07 |
| NEC MultiSync I JC-1401P3A | Shadow Mask | 0.31 mm | 15.75–35 kHz autosync | 14" | https://crtdatabase.com/crts/nec/nec-jc-1401p3a |

---

## 4. Estándares de señal

### NTSC-M
- Subportadora: **3.579545 MHz** (315/88 MHz)
- Frame rate: **30/1.001 ≈ 29.970** fps; fields **59.94 Hz**
- 525 líneas (262.5 por field), ~485 activas
- H freq: **15.734 kHz**
- BW luma Y: **4.2 MHz**; banda I: 1.3–1.5 MHz (consumer ~0.5 MHz); Q: 0.4–0.6 MHz
- Fuente: https://en.wikipedia.org/wiki/NTSC

### PAL-B/G
- Subportadora: **4.43361875 MHz**
- 25 fps / 50 Hz fields, 625 líneas, 576 activas
- H freq: **15.625 kHz** (64 µs/línea)
- BW luma: **5.0 MHz**; U y V: 1.3 MHz cada
- Fuente: https://en.wikipedia.org/wiki/PAL

### SECAM
- FM alterna línea-a-línea: **Db 4.25000 MHz** / **Dr 4.40625 MHz**
- 625/25 idem PAL
- Fuente: https://uk.tech.broadcast.narkive.com/dfUTnTTe

---

## 5. Conexiones — artefactos

| Conexión | TVL efectivos | BW chroma | Artefactos | Fuente |
|---|---|---|---|---|
| RF (VHF) | NO ENCONTRADO cifra; <240 TVL típico | <0.5 MHz | snow, beat patterns | (genérico) |
| Composite NTSC | 250–420 TVL; color ~120 TVL | ~0.5 MHz práctico (1.3 teórico) | dot crawl, rainbow | https://en.wikipedia.org/wiki/Composite_video |
| Composite PAL | ~440 TVL | 1.3 MHz | Hanover bars raro, menos dot crawl | https://en.wikipedia.org/wiki/PAL |
| S-Video | 400+ TVL | igual BW pero sin cross-talk | sin dot crawl, chroma smear leve | https://www.cockam.com/vidcolor.htm |
| SCART RGB | limitado por fuente (576i/480i full) | mono completo por canal | cero artefactos | https://retrorgb.com/rgbintro.html |
| Component YPbPr | 480p/720p/1080i consumer | Pb/Pr banda ancha | cero chroma smear | https://en.wikipedia.org/wiki/Component_video |
| VGA (RGBHV) | ≥1024×768 = ~800 TVL eq. | 3 canales 25+ MHz independientes | cero | — |

**Hueco**: NTSC chroma smear width en pixels NES (necesita medición empírica con scope o cita Lottes/cgwg).

---

## 6. Shaders de referencia

| Shader | Autor | Link | Aporte |
|---|---|---|---|
| CRT-Royale | TroggleMonkey 2014 | https://github.com/libretro/common-shaders/tree/master/crt/shaders/crt-royale | AG+Slot+Shadow conmutables; gaussian beam; halation; convergence offsets. Docs: https://docs.libretro.com/shader/crt_royale/ |
| CRT-Geom | cgwg + Themaister + DOLLS | https://github.com/libretro/common-shaders/blob/master/crt/shaders/crt-geom.cg | Curvature barrel + scanlines + phosphor; base de derivados |
| CRT-Lottes | Timothy Lottes (PD) | https://github.com/libretro/glsl-shaders/blob/master/crt/shaders/crt-lottes.glsl | Brightness redistribution; shadow mask rotada 90°; intencionalmente sin optimizar |
| CRT-Guest-Advanced | guest.r | https://github.com/guestrr/Libretro-Retroarch-SLANG/tree/main/crt-guest-dr-venom2 | LUT colors + perfiles cromáticos históricos; raster bloom |
| crtemu.h | Mattias Gustavsson (PD) | https://github.com/mattiasgustavsson/libs | Single-header C, sin deps |

**Lottes GDC 2013**: paper PDF NO ENCONTRADO público. Charla en GDC Vault de pago. Código en libretro es la referencia real. Trabajo posterior documentado en https://blurbusters.com/crt-simulation-in-a-gpu-shader-looks-better-than-bfi/

---

## 7. Hooks de presentación

### ReShade (Windows)
- DLL wrapping: archivo renombrado a `dxgi.dll` (D3D11) / `d3d9.dll` / `opengl32.dll` colocado junto al .exe target.
- Hook real en `IDXGISwapChain::Present`.
- Código: https://github.com/crosire/reshade/blob/c0a9237c6a32e4e2166c3ed38c0fdf5979b8172f/source/d3d11/d3d11.cpp

### Vulkan layers (Win+Linux)
- Manifest JSON con `name`, `type` (GLOBAL/INSTANCE), `library_path`, `api_version`, `enable_environment` (implicit layers).
- `VK_LAYER_PATH` / `VK_ADD_LAYER_PATH` env vars.
- Hook `vkQueuePresentKHR` exportando misma firma desde `vkGetDeviceProcAddr`.
- Spec: https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderLayerInterface.md
- Guía: https://renderdoc.org/vulkan-layer-guide.html

### LD_PRELOAD (Linux OpenGL)
- Hook canónico: `glXSwapBuffers` (X11) y `eglSwapBuffers` (Wayland/EGL).
- `dlsym(RTLD_NEXT, "glXSwapBuffers")` para resolver el real.
- Referencia: **glx_hook** (derhass) https://github.com/derhass/glx_hook
- FPS capper público con LD_PRELOAD: **libstrangle**

### DXGI Desktop Duplication
- `IDXGIOutputDuplication::AcquireNextFrame` con timeout en ms.
- Sólo devuelve frame cuando el desktop cambia.
- Overhead cuantificado NO ENCONTRADO oficial; reportes informales hasta 4 frames de skip mal calibrado: https://learn.microsoft.com/en-us/answers/questions/459716/desktop-duplication-api-skip-frame-issue-and-slown
- Tooling de medición: AMD FLM https://gpuopen.com/flm/

---

## 8. Efectos dependientes de CRT (verificación)

### Sonic 1 — cascadas Green Hill (confirmado)
Líneas verticales alternas con paleta cambiante. Composite las funde.
- Comparativa visual: https://x.com/CRTpixels/status/1368235112387604481
- Análisis: https://rasterscroll.com/mdgraphics/graphical-effects/transparency/
- HN: https://news.ycombinator.com/item?id=25358698

### Silent Hill PS1 — niebla
Factual: dithering agresivo, composite lo difumina mejor. **Intencionalidad declarada por Konami NO ENCONTRADA**.
- MVG: https://retrorgb.com/mvg-playstation-dithering-explained.html (403 a fetch automático, OK desde navegador)
- Patch anti-dither: https://github.com/alex-free/psx-undither
- Visual: https://x.com/CRTpixels/status/1454903312613060614

### Mega Drive otros
- Yu Yu Hakusho: Makyo Toitsusen — niebla composite-dependent
- Battle Mania 2 — flicker frames alternos (depende persistencia fosforo)
- Mega Turrican — shadow mode
- Fuente: https://rasterscroll.com/mdgraphics/graphical-effects/transparency/

### Antialiasing analógico arcade
Documentado como filtro pasa-bajo del ancho de banda finito. **Sin cita primaria de Capcom/SNK declarándolo como intencional**.
- Discusión: https://www.patreon.com/posts/controversy-cps-89245755

### Rainbow banding Mega Drive
Defecto del encoder CXA1145 (model 1); CXA1645/CXA2075 lo reducen.
- https://forums.nesdev.org/viewtopic.php?t=24447
- https://consolemods.org/wiki/Genesis:Video_Output_Notes

---

## Huecos para investigación manual

1. EIA RS-160/RS-179 / JEDEC JEP116 — comprar o biblioteca técnica.
2. Lottes GDC 2013 — GPU Pro 4 capítulo o GDC Vault.
3. Master System 256×192 PAR — SMSPower necesita búsqueda manual.
4. PVM-20M2/20M4 dot pitch — Scribd paywall parcial.
5. K7000 / G07 dot pitch numérico — descargar PDFs manualmente.
6. RetroRGB TVL numbers exactos — abrir en navegador (Cloudflare bloquea fetch).
7. Lab Guys World CRT phosphor PDF — tabla canónica fósforos.
8. NTSC chroma smear width en pixels — medición empírica con scope o cita cgwg/Lottes.
