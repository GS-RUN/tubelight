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

- **F+I — Capture by region or target window** (task #21). Two new modes:
  - `--overlay-region` opens a Snipping-Tool-style rectangle selector; the overlay positions/sizes to that.
  - `--overlay-target <title|pid>` follows a specific app window; if it moves or resizes, the overlay tracks it.
- **B — Pass 5 history-FBO temporal persistence** (task #23). Per-channel exponential decay using `decay_ms_{r,g,b}` from the profile. Visible "warm trail" effect when moving windows quickly. Needs a ping-pong FBO refactor in `core/pipeline.cpp`.
- **E — Audio** (task #25). Miniaudio header-only library, flyback whine (~15.7 kHz NTSC / 15.6 kHz PAL) modulated by frame luminance, degaussing thump on profile change.
- **J — Physics effects iteration** (task #26):
  - Heavier beam bloom on bright pixels (intensity-dependent Gaussian widening).
  - Voltage blooming dynamic (frame-mean luminance → beam width, screen tries to shrink when fully white).
  - Magnetic / thermal interference temporal (more pronounced).
  - Sub-pixel colour blending (specifically test the Sonic Green Hill cascade demo).
  - Faux analog antialiasing.

### Mid-priority polish

- HUD overlay showing currently-active profile + signal in a corner during normal use (toggleable).
- Per-profile preset save: "Save current as new preset..." button → writes a JSON in `%APPDATA%\Tubelight\profiles\crts\`.
- Bezel overlay (textured PNG of a real CRT frame around the picture) — depends on aspect snap behaviour above.
- Cleanup of unused `kHotkeyQuit / kHotkeyFreeze / kHotkeyAllOn / kHotkeyPass1` constants in `overlay_mode_win.cpp` (dead since the keyboard hook refactor).

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
