# Tubelight v0.1.4

**Date**: 2026-05-27
**Author**: Alonso J. Núñez (GS·RUN)
**License**: PolyForm Noncommercial 1.0.0 — commercial: `gsrun.editor@gmail.com`
**Supersedes**: v0.1.3 — first cut of [ADR-0001](https://github.com/GS-RUN/tubelight/blob/main/docs/adr/0001-always-on-recordable-and-clickthrough.md)

## Highlights

- **Recordable is always-on**. No more `Ctrl+Alt+R` hotkey. Snipping
  Tool, Game Bar, and OBS Display Capture see the processed overlay
  automatically.
- **Click-through is mode-dependent, no longer a user toggle**.
  `--overlay` (windowed) lets you drag/resize via the native Win32 title
  bar and borders. `--overlay-target`, `--overlay-region` and
  `--overlay-fullscreen` are click-through, as before.
- **Magnification API `src_rect_` bug fixed**. Previously, recordable
  mode kept sampling the full monitor even after attaching to a smaller
  target / region. Now the rect updates on every attach.

## Why

ADR-0001 (2026-05-27) replaces the two user-toggle hotkeys (`Ctrl+Alt+R`
and `Ctrl+Alt+C`) with always-on behaviour because:

1. The combined user model "remember to press R before recording, press
   C if you want input to pass through" was opaque and gated common
   use cases on hotkey knowledge.
2. The `Ctrl+Alt+C` toggle in windowed mode created an unrecoverable
   state — once activated, the title bar was click-through too and the
   window couldn't be moved.
3. The Magnification API source rect was hardcoded to the full monitor
   in `init()` and never updated on runtime re-attach, so any user who
   combined recordable mode with `Ctrl+Alt+T` got the wrong area
   captured.

## Migration

If your `%APPDATA%\Tubelight\settings.json` from v0.1.3 contained
`"recordable": true` or `"clickthrough_user": true`, both fields are now
silently ignored on load. No file rewrite is performed; the obsolete
keys are dropped on the next normal save.

If you relied on toggling click-through within windowed mode, switch to
`--overlay-target <window title>` or `--overlay-region X,Y,W,H` at
launch — both modes are click-through by design.

## Detailed changes

### Changed (breaking UX)
- `Ctrl+Alt+R` hotkey removed. The LL keyboard hook no longer wires it.
- `Ctrl+Alt+C` hotkey removed. The LL keyboard hook entry and the
  `WndProc` fallback path both deleted.
- Menu Audio tab "Recordable" checkbox replaced by greyed-out info
  line. Click-through checkbox same treatment.
- Help tab keyboard shortcuts list no longer mentions `R` or `C`. An
  explanatory line points at ADR-0001.
- Capture pipeline always routes through Magnification API with our
  HWND in `MagSetWindowFilterList(MW_FILTERMODE_EXCLUDE, ...)`.
  `WDA_EXCLUDEFROMCAPTURE` is no longer set on the overlay window.

### Fixed
- New `MagCapture::set_source_rect(x, y, w, h)`. Called from
  `do_attach_target()`, `do_attach_region()`, `do_toggle_fullscreen()`
  (entering), `do_detach_target()` and `do_detach_region()` (returning
  to full-monitor sampling).

### Internal
- `g_recordable_mode` atomic defaults to `true` in its declaration and
  is forced to `true` at startup. The branch in `grab_source()` that
  reads it is intentionally kept for ABI / future-proofing.
- `apply_clickthrough_user()` is now a no-op stub. The original
  `WS_EX_TRANSPARENT` toggle code is preserved inside an `if (false)`
  block for archaeological reference; will be removed in the
  D3D11/D3D12 backend cut (ADR-0002+).

## Distribution layout

```
tubelight-0.1.4-win64.zip
├── tubelight.exe                (v0.1.4)
├── epoxy-0.dll
├── glfw3.dll
├── profiles/                    (16 CRT + 7 signal)
├── docs/
│   ├── adr/
│   │   ├── README.md
│   │   └── 0001-always-on-recordable-and-clickthrough.md   (new)
│   └── manual/                  (HTML + PDF + TXT + 42 PNG assets)
├── CHANGELOG.md
├── LICENSE
└── README.md
```
