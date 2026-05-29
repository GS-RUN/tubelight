# Tubelight v0.2.0-rc.0

**Date**: 2026-05-29
**Author**: Alonso J. Núñez (GS·RUN)
**License**: PolyForm Noncommercial 1.0.0 — commercial: `gsrun.editor@gmail.com`
**Supersedes**: v0.2.0-beta.0

> **Release candidate**: the DX12 overlay path (T5.5) is feature-complete
> and smoke-verified on RTX 2080 Ti FL 12_2. This RC is published as a
> prerelease for wider testing ahead of v0.2.0 stable (Phase 3e bench).

## Highlights

Closes **Phase 3d** of ADR-0002. The real overlay now runs the full
8-pass CRT pipeline on **Direct3D 12** — not just the standalone
`--wgc-test` smoke. Combine `--renderer dx12` with any `--overlay*`
mode and Tubelight captures via **Windows.Graphics.Capture (WGC)**,
unwraps each frame to a D3D12 resource through **D3D11On12** with zero
CPU copy, and drives the same shader cascade that the OpenGL path does.

The OpenGL overlay is unchanged and remains the default.

## What's new

### DX12 overlay (`--overlay* --renderer dx12`)

- **Full pipeline on D3D12**: the 8-pass CRT cascade runs on the D3D12
  backend shipped in v0.2.0-beta.0; the overlay is now wired to it.
- **WGC capture by mode**:
  - `--overlay-target <title|pid>` → per-window capture (`CreateForWindow`).
    The overlay follows the target window's position every frame and
    exits when the target closes.
  - `--overlay-fullscreen` / `--overlay` / `--overlay-region` →
    per-monitor capture (`CreateForMonitor`), monitor chosen with
    `--monitor <index>`.
- **No self-capture feedback**: the overlay window is excluded from WGC
  via `WDA_EXCLUDEFROMCAPTURE`.
- **Global hotkeys** (focus-independent, via the low-level keyboard
  hook): `Ctrl+Alt+Q` quit, `Ctrl+Alt+1..8` toggle pass, `Ctrl+Alt+0`
  all on, `Ctrl+Alt+F` freeze.
- **Non-intrusive window**: borderless modes use
  `WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW` so the overlay doesn't steal
  focus or clutter the taskbar.

### CLI

- `--renderer dx12` now drives the overlay (was beta-only / shader-only).
- `--help` and the `--renderer` error message updated; they previously
  described dx12 as a non-driving skeleton.

## Known limitations (deferred to v0.2.1 / Phase 4a)

- **No cross-process mouse click-through** in DX12 mode. Layered windows
  (`WS_EX_LAYERED`) are incompatible with the DXGI flip-model swap chain,
  so true click-through requires DirectComposition (Phase 4a). Until
  then the DX12 overlay sits on top without passing clicks through.
- **No ImGui menu** under DX12 — the in-app menu stays OpenGL-only.
  Control the DX12 overlay with the global hotkeys above.
- **Region / windowed modes capture the whole monitor** (WGC has monitor
  granularity); a true sub-rect crop lands in v0.2.1.
- **Target tracking is position-only** — if the target window is
  *resized*, the overlay keeps its launch size (WGC frame-pool recreate
  deferred).
- **HiDPI** (>100% scaling) not yet validated for the DX12 swap chain.

## Verification

On NVIDIA RTX 2080 Ti, FL 12_2:

- `--wgc-test` regression smoke green (CRT-processed monitor capture PNG).
- `--overlay-fullscreen`, `--overlay` (windowed) and
  `--overlay-target` all render the first frame and run steadily.
- Injected `Ctrl+Alt+Q` exits cleanly (147 frames rendered, keyboard
  hook thread joined without hang).
- OpenGL overlay path unchanged (DXGI Desktop Duplication still ready).

## Distribution layout

Same as v0.2.0-beta.0. `tubelight-0.2.0-rc.0-win64.zip` (~55 MB).

## Next phases (per ADR-0002)

| Phase | Ships in |
|---|---|
| 3e — bench DX12 vs GL + v0.2.0 stable | v0.2.0 |
| 4a — DirectComposition chrome/body + DX12 click-through + ImGui | v0.2.1 |
| 5a — HDR10 scRGB FP16 pipeline (blocked on phosphor spectra) | v0.3.0 |
| 6a — VRS + async compute | v0.3.1 |
| 7a — Slang single-source shaders (closes cross-API PSNR gap) | v0.3.2 |
