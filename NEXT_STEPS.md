# Next Steps — for the next session

Repo: **https://github.com/GS-RUN/tubelight** (PRIVATE) — `main` builds and runs end-to-end on Windows with the full overlay + menu + capture pipeline.

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
- **Bezel** — DONE via programmatic SDF (5 styles selected automatically per profile category, overridable in menu, visible in Fill mode via a thin window-edge border). docs/BEZELS.md documents the photo-real PNG alternative for v1.1. Done sesión 3.
- **Click-through toggle in windowed mode** — DONE. Ctrl+Alt+C + menu checkbox toggles WS_EX_TRANSPARENT so the user can interact with whatever's underneath while keeping the CRT effect on top. Persisted. Done sesión 3.
- **Per-profile fine-tuning** — DONE. Each colour CRT id gets a tailored set of scanline / mask / bloom / halation / beam / persistence defaults (PVM crisp, BVM cleaner, FW900 milder + HD raster, 1084S warmer/softer, X68K PVM-class, MultiSync 1990s VGA, K7000 arcade gritty). Done sesión 3.
- **Video recording sources** — DONE. Menu "Record source" combo selects between Overlay view (CRT-effect, glReadPixels), Full monitor (raw DXGI BGRA), or Custom monitor-relative rect. Lets the user record areas larger than the overlay window. Done sesión 3.
- **Low-latency (vsync off)** — DONE. glfwSwapInterval(0) by default (saves ~16 ms at 60 Hz). Menu checkbox toggles back to vsync. Persisted. Done sesión 3.

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
