# Tubelight v0.1.5

**Date**: 2026-05-27
**Author**: Alonso J. Núñez (GS·RUN)
**License**: PolyForm Noncommercial 1.0.0 — commercial: `gsrun.editor@gmail.com`
**Supersedes**: v0.1.4

## Highlights

- **ADR-0002 published**: architectural blueprint for the v0.2.0+
  rendering and capture rewrite. Documents the move to D3D12 +
  DirectComposition + Windows Graphics Capture + scRGB FP16 → HDR10 +
  Variable Rate Shading + async compute pairing + Slang shader sources.
  Includes the full 10-phase rollout (2a → 7a) with per-phase
  deliverables.
- Small CPU saving in idle state: ImGui frame cycle is skipped when
  no UI is on screen.

## Why this release exists

1. **Commit the architectural direction in writing**. Future sessions
   and contributors need a single anchor document explaining *why* we
   pick D3D12 over D3D11, *why* WGC over DXGI Duplication, *why* Slang
   as the shader source language. ADR-0002 is that document — 200+
   lines covering decisions, alternatives, risks, and the multi-release
   plan.

2. **Sets up the conditional-rendering pattern** used everywhere by
   the D3D12 backend. The ImGui idle-skip is the first step.

## Changes

### Added
- `docs/adr/0002-d3d12-dcomp-wgc-hdr-slang-blueprint.md` (new ADR).
- `CHANGELOG.md [0.1.5]` section.

### Changed (perf)
- `src/overlay/overlay_mode_win.cpp` main loop now wraps the entire
  `menu.begin_frame() ... menu.end_frame_to_screen()` block in a
  visibility check. When the menu is closed, the HUD is off, no toast
  is on and no recording is in flight, ImGui's per-frame work is
  skipped entirely. Saves ~80-150µs per idle frame (~0.5% sustained
  CPU at 60 Hz).

### Internal
- CMake project version 0.1.4 → 0.1.5.
- `kVersion` and ImGui Help tab footer bumped to match.
- `manual.json` meta version bumped to 0.1.5. Manual HTML/PDF/TXT
  regenerated.

## What does NOT ship yet

ADR-0002 lists 10 implementation phases. v0.1.5 is **Phase 2a only**.
The rest:

| Phase | Ships in |
|---|---|
| 2b — PBO double-buffer screenshot+record | v0.1.6 |
| 2c — merge passes 0+1 + skip pass 5 identity | v0.1.7 |
| 3a — `IRenderBackend` abstraction + GL backend wrap | v0.1.8 |
| 3b — D3D12 backend skeleton | v0.2.0-alpha |
| 3c — D3D12 backend full pipeline | v0.2.0-beta |
| 3d — WGC capture native | v0.2.0-rc |
| 3e — bench + v0.2.0 stable | v0.2.0 |
| 4a — DComp chrome / body separation | v0.2.1 |
| 5a — HDR10 pipeline | v0.3.0 |
| 6a — VRS + async compute | v0.3.1 |
| 7a — Slang shader migration | v0.3.2 |

## Distribution layout

```
tubelight-0.1.5-win64.zip
├── tubelight.exe                (v0.1.5)
├── epoxy-0.dll
├── glfw3.dll
├── profiles/                    (16 CRT + 7 signal)
├── docs/
│   ├── adr/
│   │   ├── README.md
│   │   ├── 0001-always-on-recordable-and-clickthrough.md
│   │   └── 0002-d3d12-dcomp-wgc-hdr-slang-blueprint.md   (new)
│   └── manual/                  (HTML + PDF + TXT + 42 PNG assets)
├── CHANGELOG.md
├── LICENSE
└── README.md
```
