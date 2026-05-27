# Tubelight — Manual de Usuario PENDIENTE

Estado: planificación cerrada (fases 1 / 2 / 3 / 3.5 del skill
`app-manual-forge` ya superadas). Pendientes las fases 4 (redacción
sección a sección) y 5 (ensamblado + emisión `.html` + `.pdf` + `.txt`).

---

## Cómo retomar en la próxima sesión

Una sola frase basta para que el skill `app-manual-forge` reanude
desde donde lo dejamos:

> *"Usa app-manual-forge en D:\AgentWorkspace\Tubelight retomando el
> índice y el shotlist aprobados en docs/manual/PENDING.md"*

La skill leerá este fichero, no volverá a pedir gate humano (#1 y #2
ya están aprobados aquí abajo) y entrará directamente a Fase 4 ó 4+5
según elijas (capturar real o `TODO_SCREENSHOT`).

---

## Parámetros aprobados

| Campo | Valor |
|---|---|
| `target_path` | `D:\AgentWorkspace\Tubelight` |
| `output_dir` | `D:\AgentWorkspace\Tubelight\docs\manual` |
| `archetype` | `desktop-app` |
| `lang_default` | `es` (bilingüe `es` + `en`) |
| `depth` | `standard` (cambiable a `exhaustive`) |
| `capture_mode` | `auto` (supervisado con Windows-MCP) — o `manual` si lo prefieres |
| `headed` | `true` |
| `allow_desktop_video` | `false` (usar `frames N` para animaciones) |
| `emit_pdf` | `true` |
| `emit_integration` | `true` |
| `meta.app_name` | "Tubelight" |
| `meta.version` | `v0.1.0` |
| `meta.git_tag` | `v0.1.0` (lookup `git rev-parse v0.1.0` al ensamblar) |
| `meta.git_sha` | dinámico (`git rev-parse --short HEAD` al ensamblar) |
| `meta.author` | "Alonso J. Núñez (GS·RUN)" |
| `meta.license` | `LicenseRef-PolyForm-Noncommercial-1.0.0` |
| `meta.commercial_contact` | `gsrun.editor@gmail.com` |
| `meta.logo` | `docs/branding/logo.svg` (si existe; si no, generar marca tipográfica) |
| `meta.theme_vars` | heredar paleta `docs/branding/` (navy + amber) |

---

## ÍNDICE APROBADO (gate humano #1)

```
TUBELIGHT v0.1.0 — MANUAL DE USUARIO

  1. Bienvenida
     · Qué es Tubelight
     · Casos de uso
     · No-objetivos (qué NO hace)

  2. Instalación
     · Requisitos (Win11 26100+, GPU OpenGL 4.5, ffmpeg opcional)
     · Descarga e instalación (zip vs build local)
     · Primera ejecución
     · Desinstalar

  3. Tour de la interfaz
     · Ventana overlay (windowed por defecto)
     · Menú in-app (5 tabs: Profile · Image · Capture · Audio · Help)
     · HUD status (top-right, Ctrl+Alt+H)
     · Toast notifications (lower-left)

  4. Tu primer overlay CRT
     · Paso a paso: abrir, elegir perfil, ajustar intensidad
     · Comparativa antes/después
     · Cómo "ver" la diferencia (zoom + scanlines + máscara)

  5. Modos de overlay
     · Windowed (default) — ventana arrastrable y redimensionable
     · Fullscreen runtime (Ctrl+Alt+Enter, IF-safe)
     · Target window (Ctrl+Alt+T, follow another app's HWND)
     · Region fija (--overlay-region x,y,w,h)

  6. Perfiles de CRT — catálogo de los 16 monitores
     6.1  Sony PVM-8220 (Trinitron 8", 1985)
     6.2  Sony PVM-20M4 (Trinitron 20", 1996)
     6.3  Sony BVM-20F1U (Broadcast 20", Super Fine Pitch)
     6.4  Sony GDM-FW900 (Trinitron 24" 16:10, 1998)
     6.5  Commodore 1084S (Slot mask 14", PAL/NTSC)
     6.6  Sharp X68000 CZ-602D (15"/14" tube, 0.39 mm)
     6.7  Sharp X68000 CZ-603D (14", drops 24 kHz)
     6.8  Sharp CZ-614D (15", multisync con audio estéreo)
     6.9  NEC MultiSync (1986, JC-1401P3A)
     6.10 NEC MultiSync 4FG (1990, 0.28 mm fine pitch)
     6.11 Wells-Gardner K7000 (arcade 13", 0.65 mm)
     6.12 Generic PVM (baseline 14")
     6.13 Terminal P31 verde (Apple II / VT100)
     6.14 Terminal P3 ámbar (IBM 5151 / WYSE)
     6.15 TV B&N P4 (vintage TV 17")
     6.16 Mac Classic (9", 1-bit P4 blanco bluish)

     Cada perfil: especs físicas verificadas (mask type, dot pitch,
     diagonal, aspect_native, h/v freq), phosphor chromaticity con
     cita de manual de servicio, era, look característico, screenshot.

  7. Perfiles de señal — los 7 paths de la cadena
     7.1  RF (broadcast TV signal, ruido + dot crawl pronunciados)
     7.2  Composite NTSC (consumer RCA, chroma smear, dot crawl)
     7.3  Composite PAL (chroma delay line, rainbow banding mínima)
     7.4  SVideo (luma + chroma separados, sin dot crawl)
     7.5  SCART RGB (RGBS, casi clean)
     7.6  Component (YPbPr, separación luma+chroma diferenciales)
     7.7  RGB VGA (passthrough limpio, pixel-perfect)

     Qué artefactos modela cada uno + cuándo usar cada uno.

  8. Ajustes finos — anatomía del menú Image
     8.1  Scanlines / beam (4 sliders: strength, beam width, gamma, count)
     8.2  Phosphor mask (color CRTs) (Type + Strength + Pitch)
     8.3  Phosphor colour (monochrome) (R/G/B tint + presets + posterize)
     8.4  Bloom / halación (color) vs Phosphor glow (mono)
     8.5  Persistence per-channel (color) vs single afterglow (mono)
     8.6  Composition (barrel + vignette + display gamma + aspect ratio + bezel)
     8.7  Pass toggles — 8 pasadas del pipeline, qué hace cada una
     8.8  Guardar como preset personalizado

  9. Capturas y grabación
     9.1  Screenshot PNG (Ctrl+Alt+S)
     9.2  Vídeo MP4 (Ctrl+Alt+V) — requiere ffmpeg en PATH
     9.3  Record source (overlay / full monitor / custom rect)
     9.4  Recordable mode (Ctrl+Alt+R) — graba con Snipping Tool / Game Bar / OBS
          · Por qué existe (WDA_EXCLUDEFROMCAPTURE + Magnification API)
          · Cómo usarlo paso a paso
          · Limitaciones (per-session, ~30-60 Hz)
     9.5  Carpeta de capturas + folder picker (Browse / Apply / Default)

 10. Audio CRT
     · Flyback whine ~15.7 kHz NTSC / 15.6 kHz PAL
     · Modulación por luminancia (más blanco = más fuerte)
     · Degauss thump al cambiar perfil

 11. Atajos de teclado — tabla completa
     · Q quit · M menu · F freeze · Enter fullscreen · T target ·
       C click-through · R recordable · H HUD · S screenshot · V video
     · 0 all passes on · 1..8 toggle individual pass

 12. Casos de uso típicos
     12.1 Emuladores standalone (Mednafen, MAME, BlastEm, openMSX)
     12.2 Emuladores libretro / RetroArch (con export .slangp)
     12.3 Juegos retro modernos (UFO 50, Sea of Stars, Shovel Knight)
     12.4 Reproductores de vídeo (mpv, VLC, navegador, YouTube)
     12.5 Terminales y código (cmd, PowerShell, WSL, VS Code)

 13. Solución de problemas
     13.1 Overlay negro / no actualiza (recordable + viejo build)
     13.2 Lag percibido en lo que se ve por debajo
     13.3 "ffmpeg not found" al grabar vídeo
     13.4 Profile no carga (mensaje de validador)
     13.5 STATUS_STACK_BUFFER_OVERRUN (legacy, fixed en v0.1.0)
     13.6 Win+Shift+S no muestra overlay — checklist
     13.7 Game Bar no graba overlay — checklist
     13.8 OBS sí graba (Window Capture WGC) — instrucciones

 14. Glosario — ~22 términos
     · aperture grille, shadow mask, slot mask
     · phosphor types (P22, P31, P3, P4, P1)
     · persistencia, scanline, dot pitch
     · gamma CRT vs gamma display
     · Trinitron, halation, bloom, barrel, vignette, bezel
     · WDA_EXCLUDEFROMCAPTURE, Magnification API
     · DXGI Desktop Duplication, posterize, voltage bloom
     · SMPTE-C primaries, EBU primaries
     · IndependentFlip, DWM compositing

 15. Créditos
     · Autor: Alonso J. Núñez (GS·RUN)
     · Repositorio: https://github.com/GS-RUN/tubelight (PRIVATE)
     · Licencia: PolyForm Noncommercial 1.0.0
     · Comercial: gsrun.editor@gmail.com
     · Fuentes citadas: crtdatabase.com, ManualsLib, archive.org,
       gamesx.com X68000 wiki, Wikipedia EIA phosphor table,
       Sony BVM operation manual, NEC MultiSync 4FG service manual,
       Commodore 1084S service manual
     · Tecnologías: Dear ImGui, GLFW, libepoxy, GLM, nlohmann/json,
       stb_image, MinHook, Magnification API, XAudio2, DXGI Desktop
       Duplication, OpenGL 4.5
```

---

## SHOTLIST APROBADO (gate humano #2)

Backend: **Windows-MCP** (Tubelight es nativo Windows GLFW+ImGui).
Viewport overlay: **1280×960** (default `--size 1280 960`). Modo
default `static`; `frames N` para animaciones.

```
═══════════════════════════════════════════════════════════════════════
§1 Bienvenida
  hero-01   Overlay sobre escritorio limpio + pvm-8220              static
            (acciones: launch, wait 2s, capture)

§2 Instalación
  inst-01   Carpeta D:\AgentWorkspace\Tubelight con tubelight.exe   static (Explorer)
  inst-02   Primera ejecución sin perfil — DXGI ready en consola    static

§3 Tour interfaz
  ui-01     Overlay con menú ImGui cerrado                          static
  ui-02     Menú abierto en tab Profile                              static
  ui-03     Menú abierto en tab Image (Phosphor mask + sliders)      static
  ui-04     Menú abierto en tab Capture                               static
  ui-05     Menú abierto en tab Audio                                 static
  ui-06     Menú abierto en tab Help                                  static
  ui-07     HUD top-right activo (perfil + signal + mode)             static
  ui-08     Toast notification visible (Ctrl+Alt+S)                   static

§4 Primer overlay
  first-01  Antes: ventana de Notepad sin overlay                    static
  first-02  Después: Tubelight pvm-8220 + composite_ntsc encima      static
  first-03  Slider Intensity x = 0 (passthrough)                     static
  first-04  Slider Intensity x = 1 (default)                         static
  first-05  Slider Intensity x = 2 (full retro)                      static

§5 Modos de overlay
  mode-01   Windowed default                                         static
  mode-02   Fullscreen runtime (Ctrl+Alt+Enter)                      static
  mode-03   Target window — Tubelight siguiendo Notepad              static
  mode-04   Region fija — rect 800x600 en monitor                    static

§6 Perfiles CRT (16 capturas — galería con mismo contenido fuente)
  prof-pvm-8220, prof-pvm-20m4, prof-bvm-20f1u, prof-fw900,
  prof-1084s, prof-x68k-602d, prof-x68k-603d, prof-cz-614d,
  prof-multisync-1, prof-multisync-4fg, prof-k7000, prof-generic-pvm,
  prof-p31-green, prof-p3-amber, prof-p4-bw-tv, prof-mac-classic
  → mismo source (texto Notepad o gradient image) bajo cada perfil    static

§7 Perfiles de señal (7 capturas — mismo perfil PVM-8220)
  sig-rf, sig-composite-ntsc, sig-composite-pal, sig-svideo,
  sig-scart-rgb, sig-component, sig-rgb-vga                          static

§8 Ajustes finos (selección de comparativas)
  fine-scanlines-low / high      (slider 0.0 vs 0.9)                  static
  fine-mask-shadow / aperture / slot / diamond                        static
  fine-bloom-off / strong                                              static
  fine-persistence-off / long-warm-trail                              static
  fine-barrel-flat / aggressive                                       static
  fine-aspect-4-3-letterbox / fill                                    static
  fine-mono-tint-r-g-b-sliders                                          static
  fine-posterize-mac-classic-1bit                                      static
  fine-pass-toggles-mask-off                                            static
  fine-save-preset-form                                                  static

§9 Capturas y grabación
  cap-01    Menú Capture tab con folder picker                        static
  cap-02    Browse dialog (Windows native folder picker)        TODO_SCREENSHOT
            (modal nativo OS, no capturable por Windows-MCP)
  cap-03    Toast "Screenshot saving: <nombre.png>"                   static
  cap-04    Record source combo desplegado                            static
  cap-05    Recordable mode ON — toast confirmación                   static
  cap-06    REC dot rojo durante grabación MP4                        static

§10 Audio
  audio-01  Tab Audio con flyback whine + volumen                     static

§11 Atajos
  (sin captura — tabla de texto en la sección)                       n/a

§12 Casos de uso
  uc-mednafen   openMSX + Tubelight pvm-8220        TODO_SCREENSHOT  frames 3
                (necesita openMSX + Space Manbow ROM instalados)
  uc-mpv        mpv vídeo + Tubelight 1084s          TODO_SCREENSHOT  frames 3
                (necesita mpv + clip de vídeo)
  uc-terminal   PowerShell + Tubelight terminal-p31                   static
  uc-vscode     VS Code editor + Tubelight terminal-p3-amber          static

§13 Troubleshooting
  ts-01     CLI --validate-profile con perfil malformado              terminal-block
            (ejecutar comando, capturar stdout/stderr)
  ts-02     CLI con ffmpeg no detectado al grabar vídeo               terminal-block
  ts-03     Win+Shift+S sin Ctrl+Alt+R previo (texto + explicación)   n/a

§14 Glosario, §15 Créditos — sin capturas
═══════════════════════════════════════════════════════════════════════

TOTAL  ~52 PNGs estáticas + 2 tiras-de-frames (6 PNGs) + 2 terminal-blocks
TODOS marcados arriba: §9 cap-02, §12 uc-mednafen, §12 uc-mpv
TIEMPO estimado captura supervisada: 15-25 min
```

---

## Acciones automatizables por Windows-MCP

Para cada captura de overlay, el patrón base:

```
1. mcp__Windows-MCP__App   start D:\AgentWorkspace\Tubelight\tubelight.exe
                          --overlay --profile <id> --signal <id>
                          --size 1280 960
2. wait 2 s  (DXGI ready + first frame uploaded)
3. opcional: mcp__Windows-MCP__Shortcut  Ctrl+Alt+M  para abrir menú
4. opcional: navegar tabs con click coords (tabs en y≈45px del top de la ventana)
5. mcp__Windows-MCP__Screenshot  →  PNG
6. mcp__Windows-MCP__Process     stop tubelight.exe
```

Para los `frames N`, la app permanece abierta entre capturas, sólo
cambian las acciones disparadoras y los timings entre snapshots.

---

## Decisiones pendientes para la próxima sesión

1. **Capturar o `TODO_SCREENSHOT` todo**: si el ordenador donde se
   ejecute la próxima sesión NO tiene openMSX/mpv instalados, marcar
   esos como TODO y completar a mano más tarde. El resto sí se
   puede capturar.
2. **Depth final**: `standard` (default actual) cubre todo lo importante.
   `exhaustive` añadiría:
   - Sub-sección por slider con explicación física del fenómeno
   - Tabla cita-vs-valor por perfil con URL del manual de servicio
   - Recipe-book con configuraciones pre-hechas por tipo de contenido
     (SNES NTSC / Mega Drive PAL / arcade vertical / DOS games / etc.)
3. **`emit_pdf`**: `true` por default. Requiere Playwright instalado
   (no es dependencia del proyecto Tubelight, sólo del skill al
   ensamblar). Si no está, generamos sólo HTML + TXT.
4. **`emit_integration`**: `true` por default. Genera `INTEGRATION.md`
   con código stub para abrir el manual desde la propia app (menú
   Help → Manual, abre `manual.html` en navegador). Pequeño cambio a
   `src/overlay/menu.cpp` que se propone pero no aplica sin OK.

---

## Estado actual al pausar

- Fases del skill cubiertas: 1 (discovery), 2 (archetype), 3 (índice + gate),
  3.5 (shotlist + gate).
- Pendiente: fases 4 (redacción) y 5 (ensamblado/emisión).
- Repo state: HEAD `a0aae51`, tag `v0.1.0` en `388339d` (pre-license-change).
  El tag se moverá al HEAD final del manual cuando se cierre la sesión
  siguiente.
