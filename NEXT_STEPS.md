# Next Steps — for the next session

Repo: **https://github.com/GS-RUN/tubelight** (PRIVATE) — `main` builds and runs end-to-end on Windows with the full overlay + menu + capture pipeline.

## Pendiente con planificación cerrada — manual de usuario

Skill: `app-manual-forge` (catálogo skill-anvil). Fases 1 / 2 / 3 (índice
aprobado) / 3.5 (shotlist aprobado) **ya superadas**; queda Fase 4
(redacción) + Fase 5 (ensamblado HTML + PDF + TXT bilingüe ES/EN).

Plan completo, parámetros aprobados, índice de 15 secciones, shotlist
~58 PNGs con backend Windows-MCP, decisiones pendientes y acciones
automatizables documentadas en:

  → `docs/manual/PENDING.md`

Para retomar:

> *"Usa app-manual-forge en D:\AgentWorkspace\Tubelight retomando el
> índice y el shotlist aprobados en docs/manual/PENDING.md"*

## v0.1.0 — first complete version closed 2026-05-27

HEAD `6c9d282`. User signed off: *"creo que ya tenemos la primera versión final del proyecto"*. Closes the full feature scope agreed across sessions: overlay (4 modes), 16 CRT + 7 signal profiles re-verified against service manuals, 8-pass shader pipeline, ImGui menu redesign with 5 tabs and per-widget Spanish tooltips, recordable mode via Magnification API, capture pipeline (PNG screenshot + MP4 video via ffmpeg + CRT audio), per-session click-through + recordable + low-latency toggles, target / region / fullscreen modes with hotkey + menu equivalence.

## What works right now (2026-05-26 end of session)

### Overlay modes
- **`tubelight --overlay`** (default) — resizable Win32 window. Move it like any normal window; the area underneath is captured by DXGI Desktop Duplication every frame and processed through the 8-pass CRT pipeline. Window stays topmost; clicks land on it (use Ctrl+Alt+M to open the menu).
- **`tubelight --overlay-fullscreen`** — borderless topmost click-through fullscreen overlay (original behaviour). For watching content full-screen with the CRT effect.
- `--monitor <index>` selects which display, `--size <w> <h>` sets initial window size.

### Hotkeys (low-level keyboard hook, work even with no focus)
- `Ctrl+Alt+Q` — quit
- `Ctrl+Alt+M` — toggle in-app menu
- `Ctrl+Alt+F` — freeze / unfreeze the captured frame (autorepeat debounce)
- `Ctrl+Alt+S` — save PNG screenshot of the rendered overlay
- `Ctrl+Alt+V` — start / stop MP4 video recording (ffmpeg subprocess)
- `Ctrl+Alt+R` — toggle **recordable mode**. Default OFF: overlay sets `WDA_EXCLUDEFROMCAPTURE` and uses DXGI Desktop Duplication for source. When ON: drops the WDA flag so Win11 Snipping Tool / Xbox Game Bar / OBS / `ffmpeg gdigrab` can record the overlay, AND switches source capture from DXGI to the **Magnification API** with the overlay's HWND in `MagSetWindowFilterList(MW_FILTERMODE_EXCLUDE, ...)`. Mag is the only Win32 mechanism that provides *per-capturer* exclusion (WDA is binary across DWM), so external recorders go through DWM and see the overlay normally while our internal source still ignores us → no feedback, overlay stays live. Source updates at ~30-60 Hz (Mag callback overhead). Persists between launches.
- `Ctrl+Alt+0` — re-enable all 8 passes
- `Ctrl+Alt+1..8` — toggle individual pass on/off

### In-app menu (ImGui, Ctrl+Alt+M)
- **Profiles**: CRT dropdown (16 profiles bundled) + Signal dropdown (7 profiles). Switching is live.
- **Global intensity**: 0..2.0 × multiplier scaled against the profile's natural strengths.
- **Scanlines / beam**: scanline strength, beam width, CRT gamma, scanline count (240 NTSC / 288 PAL / 480 VGA).
- **Phosphor mask**: type (shadow / aperture grille / slot / diamond / cgwg / dot-trio) + strength + pitch.
- **Bloom / halation**: per-effect sliders.
- **Composition**: barrel, vignette, display gamma, **Aspect ratio combo** (Fill window / 4:3 / 5:4 / 16:10 / 16:9 / 21:9).
- **Pass toggles**: checkboxes for all 8 passes.
- **Captures**: capture folder (text input + **Browse...** native Windows folder picker + Apply + Default). Settings persist in `%APPDATA%\Tubelight\settings.json`.

### Visual feedback HUD
- Toast in lower-left corner when a screenshot saves / video starts or stops / capture folder changes (auto-fades in 2.5 s).
- Blinking red REC dot + "REC" label in upper-left while video is recording.

### Profiles bundled (16 CRT + 7 signal)
CRT (15 + Mac Classic): pvm-8220, pvm-20m4, bvm-20f1u, commodore-1084s, sharp-x68k-cz602d, sharp-x68k-cz603d, sharp-cz-614d, wells-gardner-k7000, nec-multisync-1, nec-multisync-4fg, sony-fw900, terminal-p31, terminal-p3-amber, tv-bw-p4, **mac-classic-white** (1-bit posterize), generic-pvm.

Signal: rf, composite_ntsc, composite_pal, svideo, scart_rgb, component, rgb_vga.

Every JSON has `source.url` per Constitution C2. Monochromes have correct saturated phosphor colours (P31 green, P3 amber, P4 cool white). Mac Classic is the only one with `posterize_levels=2` (true 1-bit framebuffer).

## What's still pending — agreed for next sessions

### Top of next session (the user asked for it explicitly today)

- **Auto-resize the overlay window to match the chosen aspect ratio** — DONE 2026-05-26 sesión 3. Picking a non-Fill aspect in the menu now (a) snaps the window to that ratio keeping center + area, and (b) locks GLFW user-drag-resize to that ratio via `glfwSetWindowAspectRatio`. A "Snap window to aspect" button in the Composition section forces a re-snap on demand.
- **Runtime fullscreen toggle preserving aspect** — DONE 2026-05-26 sesión 3. Menu button "Go fullscreen (Ctrl+Alt+Enter)" + the Ctrl+Alt+Enter hotkey flip between windowed and borderless monitor-filling fullscreen at runtime. Fullscreen keeps the current `target_aspect` (letterboxes via the Pass 6 shader); leaving fullscreen restores the saved windowed pos/size. Unlike `--overlay-fullscreen` (CLI), the runtime fullscreen stays focusable so the menu still works.
- **Live rendering during window move / resize** — DONE 2026-05-26 sesión 3. WndProc subclass installs a ~16 ms WM_TIMER between WM_ENTERSIZEMOVE / WM_EXITSIZEMOVE that drives the same per-frame DXGI grab + pipeline + swap the main loop runs. The picture under the window now follows in (near) real time while you drag the title bar, instead of freezing on the last frame.

### Big items still open (planned earlier, not done)

- **F+I — Capture by region or target window** — DONE.
  - `--overlay-target <title>` / `--overlay-target-pid <pid>` + `Ctrl+Alt+T` hotkey + menu "Target window" section. Overlay follows the target's client rect every frame, click-through. Done sesión 3.
  - `--overlay-region x,y,w,h` + menu "Region (fixed rect)" section pins the overlay to a fixed monitor-relative rectangle. Done sesión 3.
- **B — Pass 5 history-FBO temporal persistence** — DONE. history_fbo_ + glCopyTexSubImage2D blit per frame. Per-channel decay via persistence_strength × ratios (R=1.0, G=0.5, B=0.5 for P22 colour CRT warm-trail). Menu sliders. Done sesión 3.
- **E — Audio** — DONE. XAudio2 streaming source voice + worker thread, sine at h_freq_khz×1000, amplitude modulated by frame mean luminance, degauss thump on profile change. Menu enable + volume slider, off by default, persisted. Done sesión 3.
- **C — Physics effects** — DONE. Voltage bloom (u_frame_mean_lum widens beam +30 % at full white), heavier intensity-dependent bloom (1.0→2.5×), sub-pixel AA in Pass 2 (4-tap average), magnetic interference ×3-4 amplitude in Pass 6 + 0.5 Hz hum ripple. Done sesión 3.

### Mid-priority polish

- **HUD** — DONE. Top-right floating box shows profile + signal + mode (Windowed / Fullscreen / Region / Tracking "<title>"; appends "(click-through)" when Ctrl+Alt+C is on). Ctrl+Alt+H toggle + menu checkbox + persist. Done sesión 3.
- **Save current as preset** — DONE. Inline menu form (id + display_name + Save button). Writes %APPDATA%\Tubelight\profiles\crts\<id>.json based on the current profile + live params. Done sesión 3.
- **Bezel** — SDF deemed unsatisfactory by user, OFF by default. PNG loading hook still wired (`assets/bezels/<profile_id>.png`); if user sources good photo-real images the shader picks them up automatically. SDF styles 1-5 remain in the menu Combo for opt-in.
- **Click-through toggle in windowed mode** — DONE (3 iterations). Ctrl+Alt+C + menu checkbox now uses `WS_EX_LAYERED | WS_EX_TRANSPARENT + LWA_ALPHA 255` (DWM composite hit-test) — the only mechanism that crosses process boundaries on Windows. WS_EX_LAYERED is added once and left permanent (runtime removal proved unreliable); only TRANSPARENT + NOACTIVATE flip on each toggle. SetWindowPos(SWP_FRAMECHANGED) so the new ex-style takes effect immediately. Per-session only (no persistence at startup) — avoids the "I can't drag the title bar" surprise on next launch. Verified by user with openMSX.

  **Lesson learned (don't repeat)**: WM_NCHITTEST returning HTTRANSPARENT does NOT cross process boundaries — per MSDN it only "sends the message to underlying windows in the same thread". Was used 2 iterations and confirmed insufficient. NCHITTEST handler is now kept only as harmless backup for any same-thread case.
- **Per-profile fine-tuning** — DONE. Each colour CRT id gets a tailored set of scanline / mask / bloom / halation / beam / persistence defaults (PVM crisp, BVM cleaner, FW900 milder + HD raster, 1084S warmer/softer, X68K PVM-class, MultiSync 1990s VGA, K7000 arcade gritty). Done sesión 3.
- **Video recording sources** — DONE. Menu "Record source" combo selects between Overlay view (CRT-effect, glReadPixels), Full monitor (raw DXGI BGRA), or Custom monitor-relative rect. Lets the user record areas larger than the overlay window. Done sesión 3.
- **Low-latency (vsync off)** — DONE but DEFAULT REVERTED. vsync ON by default — the previous "vsync off by default" decision saturated CPU/GPU and dragged the whole desktop down to ~1 fps. Now: vsync ON (60 fps cap), menu checkbox opts in to vsync off, AND in that case a soft 240 fps cap via `std::this_thread::sleep_until` keeps the loop from spinning unboundedly. One-time migration in the new build resets persisted `low_latency` to false on startup.

### Big items still open

- **Factory-spec audit of all 16 CRT profiles against service manuals** — DONE 2026-05-27. All 16 JSON profiles re-derived from service manuals + authoritative sources (crtdatabase.com per-model pages, ManualsLib service manuals, archive.org of NEC/Sony manuals, gamesx.com X68000 wiki, Wikipedia EIA phosphor table). Key corrections this pass: commodore-1084s mask_type shadow→slot (Philips CM8833 OEM tube is slot-style), generic-pvm pitch 0.45→0.25 mm (previous was transcription error — PVM-class never had pitch that coarse), nec-multisync-4fg source upgraded to archive.org service manual (confirmed 4:3 native at 1024×768, not 5:4), sharp-cz-614d diag 14→15" tube (14" viewable), sharp-x68k-cz602d diag 14→15" tube, sharp-x68k-cz603d documented as supporting only 15.98+31.469 kHz (drops 24.838 kHz from 602D), sony-fw900 pitch 0.25→0.23 mm centre with note about 0.27 mm at edges (variable grille pitch), sony-bvm-20f1u source upgraded to ManualsLib service manual page 61 (0.30 mm Super Fine Pitch confirmed), p22 colour CRT per-channel decay across all 9 colour profiles corrected from (1.0, 0.08, 0.08) ms — which was an off-by-magnitude error — to (1.0, 2.0, 1.0) ms typical, terminal-p31 chromaticity (0.260, 0.530)→(0.226, 0.555), terminal-p3-amber chromaticity (0.560, 0.430)→(0.523, 0.469), tv-bw-p4 decay 0.5→0.06 ms (P4 short persistence ~60 µs canonical), mac-classic-white chromaticity (0.295, 0.330)→(0.265, 0.285) (Apple compact Macs notably blue-shifted vs NTSC P4 reference). Items still soft (within EIA TEP-116 tolerance ±0.02 xy on monochromes since the underlying standard is paywalled): exact phosphor CIE xy for P3/P4/P31/P1 — values used are Wikipedia phosphor-table consensus. All 16 profiles validate against the schema (`tubelight --validate-profile`).

### Optional / deferred for v1.1

- PBO double-buffer in `glReadPixels` (during video recording) — eliminates the ~3-6 ms GPU stall per recorded frame.
- Linux build local + cross-platform bit-comparable parity (M7).
- Photo-real bezel PNGs sourced from Wikimedia Commons (see docs/BEZELS.md for the per-profile coverage matrix + integration plan).
- Refactors: stringly-typed `aspect_native` → enum on CRTProfile; full MenuIO struct replacing the remaining direct params.

## Build commands (Windows)

```
scripts\build_windows.bat
```

Auto-locates VS 2022 BuildTools + vcpkg (in `C:\vcpkg`) and produces `build\windows-vcpkg\Release\tubelight.exe`. Run as:

```
build\windows-vcpkg\Release\tubelight.exe --overlay [--profile <id>] [--signal <id>]
build\windows-vcpkg\Release\tubelight.exe --overlay-fullscreen
build\windows-vcpkg\Release\tubelight.exe --validate-profile profiles/crts/<file>.json
build\windows-vcpkg\Release\tubelight.exe --export-slangp out.slangp --profile <id> --signal <id>
```

## Verified on real hardware

- VS 2022 BuildTools 17.0 + MSVC 19.44 + vcpkg HEAD `7e99dc22` + CMake 4.2.3 on Windows 11 (Microsoft Windows 10.0.26200).
- Monitor 1920×1200 — overlay window 1280×960 default, freely resizable.
- Profile changes propagate live: PVM-8220 (color shadow grille) ↔ terminal-p31 (saturated green) ↔ tv-bw-p4 (cool white analog) ↔ mac-classic-white (literal 1-bit pure black/white).
- Signal changes propagate live: composite_ntsc shows chroma smear, rgb_vga is passthrough.
- Ctrl+Alt+S writes a real PNG in `%USERPROFILE%\Pictures\Tubelight\` (or whatever the user picked via Browse...).
- Ctrl+Alt+V records H.264 MP4 via piped ffmpeg; toast confirms start/stop; verified file plays in any standard player.

## Known cosmetic issues to look at next session

- Monochromes still feel a touch "too tonal" for the user's taste — the posterize_levels=6 default could go lower (4 or 5) for tighter terminal feel. Mac Classic profile already at 2 (correct).
- Color profiles' mask pattern at small window sizes can alias; mask_pitch_px scaling vs window size should be revisited.
- Scanline count 240 NTSC default at 960-pixel-tall window = 4 px per scanline; that's OK but in tiny windows it loses definition. Maybe auto-scale.
