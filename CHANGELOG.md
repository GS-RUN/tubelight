# Changelog

All notable changes to Tubelight. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [SemVer 2.0](https://semver.org/).

## [Unreleased]

### Known limitations

## [0.2.0-alpha.0] — 2026-05-28

### Added (architecture)
- **Phase 3b (ADR-0002): Direct3D 12 backend skeleton**. New
  `D3D12Backend` (`src/render/backend_d3d12.{h,cpp}`) creates a D3D12
  device (FL 12_0+), DXGI 1.6 flip-discard swap chain, command queue
  + allocator + list, RTV descriptor heap, fence sync, clear, and
  Present. Validated on NVIDIA RTX 2080 Ti at FL 12_2.
- **CLI `--renderer dx12`** opens a no-API GLFW window, hands the HWND
  to `D3D12Backend`, and runs a proof-of-life loop that clears to a
  Tubelight-blue background. Falls back to OpenGL automatically if
  device creation fails — covers ADR-0002 R12 (pre-DX12 GPU, Wine
  without DXVK, etc).
- **`IRenderBackend` extended** with `begin_frame()` / `end_frame()` /
  `resize()` / `supports_pipeline()` and a `BackendInitParams` struct
  that bundles the opaque native window handle. GL backend ignores
  the handle; D3D12 requires it.
- **CMake option `TUBELIGHT_BUILD_DX12`** (default ON on Windows, OFF
  on Linux). Adds `d3d12` system library and the
  `TUBELIGHT_HAVE_D3D12` compile definition. No new vcpkg deps — the
  Windows SDK 10.0.26100 already ships everything required.

### Important limitation
- **The 8-pass CRT Pipeline still requires the OpenGL backend**.
  `D3D12Backend::supports_pipeline()` returns `false`; Pipeline rejects
  any backend that doesn't support it. Porting the passes to HLSL +
  rewriting Pipeline to use abstract resource handles lands in
  Phase 3c (target: v0.2.0-beta). `--renderer dx12 --shader-only`
  shows only the proof-of-life clear; the testcard image argument is
  accepted but not yet rendered through the pipeline.

### Why ship a non-driving backend?
Two reasons:
1. Validates windowing + adapter selection + swap chain plumbing
   end-to-end on real hardware before sinking a week into HLSL ports.
2. Surfaces `--renderer dx12` so users and CI can smoke-test D3D12
   device creation on their hardware ahead of v0.2.0 stable (early R12
   detection).

### Synchronisation
Intentionally simple in Phase 3b: one allocator, one command list, one
fence, wait-for-idle on every Present (~1 ms penalty vs. a proper
triple-buffered pipeline). Pipelining lands in Phase 3c.

## [0.1.7] — 2026-05-28

### Added (architecture)
- **Phase 3a (ADR-0002): `IRenderBackend` abstraction** — new
  `src/render/` module introducing `IRenderBackend`, `GLBackend`, and a
  `create_backend(BackendKind)` factory. `Pipeline` now routes its raw
  GL state changes (viewport, clear, default-framebuffer bind, fullscreen
  quad draw) through the backend instead of calling OpenGL directly.
  `FBO`, `Texture2D`, and `ShaderProgram` remain GL-specific concrete
  types for this phase; they get abstracted in Phase 3b when the D3D12
  backend forces it. Zero functional change vs. v0.1.6 — the binary
  renders identically.
- **CMake option `TUBELIGHT_RENDERER_GL`** (default ON). Currently the
  only supported value; build scripts and CI can already reference it.
- **CLI flag `--renderer <gl>`** — accepted by the binary, validates the
  token, and rejects anything other than `gl` (or its `opengl` alias) so
  users get a clear error rather than a silent fallback. The `dx12` token
  is explicitly reserved for v0.2.0.
- `Pipeline::set_backend()` for tests and Phase 3b code that wants to
  inject an alternative backend before `create()`.
- `Pipeline::backend_name()` for diagnostics — surfaced through
  `tubelight --version` as `(renderer: gl)`.

### Why
Rails for the D3D12 backend (ADR-0002 §3). Phase 3a is intentionally
narrow so the diff is reviewable as a refactor and the binary's behaviour
is byte-identical to v0.1.6: anyone bisecting can use 0.1.7 as a baseline
free of new pixel changes.


- Pass 5 (temporal persistence / voltage bloom / BFI) is still an identity
  shader. Real implementation requires a history-FBO refactor scheduled
  for v1.1 — see specs/PLAN.LOCKED.md F7 T7.2.
- PipeWire screencast capture is scaffolding only; D-Bus portal session
  and stream consumption ship in v1.1.
- M1 (<2 ms hook latency) is unverified — requires a real target run with
  AMD FLM or equivalent. Hook bodies are passthrough today.
- Magnification API source rect (`src/overlay/overlay_mode_win.cpp:674`)
  is hardcoded to monitor rect — not updated when overlay-target rect
  changes at runtime via `Ctrl+Alt+T`. Workaround: use the CLI flag
  `--overlay-target <title>` which initializes correctly. Fix deferred
  to v0.2.0.

## [0.1.6] — 2026-05-27

### Changed (perf)
- **Phase 2b (ADR-0002): PBO double-buffer for video recording**.
  `VideoRecorder::push_frame()` no longer blocks the main loop on
  `glReadPixels`. A 3-slot Pixel Buffer Object ring is primed at
  recording start; each frame the new read goes into the next slot
  (driver DMAs async) and the oldest slot — written 3 frames ago,
  guaranteed GPU-ready — is mapped and shoved to ffmpeg.
  Saved per recorded frame: ~3-6 ms GPU stall. At 60 Hz recording
  that's a noticeable framerate stabilisation on mid-tier GPUs and
  ~20% lower CPU under sustained capture.
  Implementation: `src/overlay/capture_to_disk.{h,cpp}` —
  `kPboCount=3`, new members `pbos_[]` / `pbo_write_idx_` /
  `pbo_filled_`. The ring is drained on `stop()` so the last 2 frames
  of the recording aren't lost. `push_frame_from_bgra()` (DXGI/Mag
  CPU-side path) is unchanged — its data is already host-resident.
  Video frames are now delayed by `kPboCount` frames (~50 ms at 60 Hz)
  before being written to the file; this is invisible in the final
  MP4 because timestamps come from the recorder, not from the time
  of write.
- **Phase 2c (ADR-0002): skip pass 5 when persistence is negligible**.
  `Pipeline::render_to_screen()` now early-skips the temporal pass
  (`fbos_[6]` bind + clear + shader dispatch + history snapshot) when
  `persistence_strength * (ratio_r + ratio_g + ratio_b) < 1e-3`.
  Profiles that hit this: `terminal-p31` (strength 0.18 × ratios ≈
  0.55 — does NOT skip), `tv-bw-p4` (strength 0.40 — does NOT skip),
  `pvm-8220` with intensity 0 (skipped because strength scales with
  intensity multiplier through `params_.persistence_strength`).
  Saved per frame when applicable: ~0.4 ms on a mid-tier GPU.

### Notes
- Original Phase 2c plan was "merge passes 0+1 into one shader". On
  closer inspection that would have **increased** texture sampling
  3× (pass 1 reads its neighbours' pass-0 outputs, so a merge would
  require computing pass 0 at the centre AND four neighbours — 9
  samples each, 45 total vs. the current 9 + 5 = 14). Plan revised.
- The screenshot path (`save_screenshot_png_async`) was NOT moved
  to PBOs. It's one-shot per user keypress, and the worker-thread
  PNG encode already hides the cost. Will revisit if needed.

## [0.1.5] — 2026-05-27

### Added
- **ADR-0002** (`docs/adr/0002-d3d12-dcomp-wgc-hdr-slang-blueprint.md`)
  — architectural blueprint of the next-generation rendering and
  capture stack: D3D12 + DirectComposition (chrome / body visuals
  separately) + Windows Graphics Capture (per-window) + scRGB FP16 →
  HDR10 output + Variable Rate Shading on bloom/halation passes +
  async compute pairing + Slang shader sources compiled to SPIRV +
  DXIL + HLSL bytecode. Implementation lands incrementally across
  v0.1.5 → v0.3.x (full plan in the ADR Phase table). No code change
  in this release beyond the doc.

### Changed (perf)
- **Skip the ImGui begin/end frame cycle when no UI is on screen**
  (`overlay_mode_win.cpp:1845`). Earlier builds always paid the
  ~30-50µs `ImGui::NewFrame()` + 50-100µs `ImGui::Render()` even when
  the menu was closed, the HUD off, no toast active and no recording.
  Now the cycle only fires when at least one is visible. Marginal
  CPU saving (~0.5% sustained on 60 Hz) but cleaner separation
  between idle and active overlay states; sets up the
  conditional-rendering pattern that the D3D12 backend will follow.

### Notes
- WS_EX_LAYERED is no longer added to the plain `--overlay` (windowed)
  window. The path that added it (Ctrl+Alt+C toggle, removed in v0.1.4)
  is gone. v0.1.5 confirms this as the intentional design — the DWM
  composition cost in windowed mode dropped accordingly. The
  layered-window path is still used for the Magnification API host and
  for target/region/fullscreen overlay modes (where WS_EX_TRANSPARENT
  is required for cross-process click-through).

## [0.1.4] — 2026-05-27

### Changed (UX / breaking)
- **Recordable mode is always-on**. The `Ctrl+Alt+R` hotkey is removed.
  Snipping Tool, Game Bar and OBS Display Capture see the overlay
  automatically, without any user action. Implementation: capture
  always routes through the Magnification API with our own HWND in
  `MagSetWindowFilterList(MW_FILTERMODE_EXCLUDE)` to break the feedback
  loop. `WDA_EXCLUDEFROMCAPTURE` is no longer used.
- **Click-through is mode-dependent, no longer a user toggle**. The
  `Ctrl+Alt+C` hotkey is removed and the menu checkbox replaced by a
  disabled info line. Policy by mode: `--overlay` (windowed) = OFF
  (drag/resize via standard Win32 title bar / borders); `--overlay-target`,
  `--overlay-region`, `--overlay-fullscreen` = ON.
- Menu Audio tab "Recordable by Snipping Tool / Game Bar / OBS" and
  "Click-through (windowed)" checkboxes are now replaced by greyed-out
  status lines documenting the new always-on / by-mode behaviour.
- Help tab shortcut list no longer mentions `Ctrl+Alt+C` or `Ctrl+Alt+R`.
  In-app footer bumped to v0.1.4.

### Fixed
- **Magnification API `src_rect_` is now updated on target / region /
  fullscreen attach** (`overlay_mode_win.cpp:712`, new
  `MagCapture::set_source_rect(x, y, w, h)`). Pre-v0.1.4, the rect was
  set once in `init()` to the full monitor and never updated when the
  overlay attached to a smaller area, so the Mag callback kept sampling
  the whole screen instead of the new target. Visible as "the recorded
  overlay is bigger / offset relative to what's actually visible".

### Removed
- Settings fields `clickthrough_user` and `recordable` in
  `%APPDATA%\Tubelight\settings.json` are now silently ignored on load.
  No migration is written; the file is just read-as-is and the
  obsolete keys are dropped on the next normal save.

### Notes
- This is the "Phase 1" cut of ADR-0001 (always-on R/C, fix Mag bug).
  Phase 2 (D3D12 + WGC capture + HDR pipeline) and Phase 3 (full chrome
  rewrite with mouse-hook drag/resize) are tracked in subsequent ADRs.

## [0.1.3] — 2026-05-27

### Changed
- **Repo is now public**. User manual updated across all instances to
  remove "PRIVATE" / "private repo" wording:
  - `docs/manual/manual.json` §2 (download): now points users to the
    public [Releases page](https://github.com/GS-RUN/tubelight/releases)
    in both ES and EN; the outdated `tubelight-0.1.0-win64.zip` example
    replaced with `tubelight-<version>-win64.zip` so future bumps don't
    desync the docs.
  - `docs/manual/manual.json` §13 (troubleshooting): "open an issue in
    the private repo" → "open a [GitHub issue](.../issues)" in both ES
    and EN, with the actual issues URL.
  - `docs/manual/manual.json` §15 (credits): repo URL no longer tagged
    `(PRIVATE)` in either language.
  - `NEXT_STEPS.md`: removed `(PRIVATE)` tag from the repo line.
- `manual.json` `meta.version` / `meta.git_tag` bumped to `0.1.3`.
- Regenerated `manual.html`, `manual.pdf`, `manual.es.txt`,
  `manual.en.txt` from the cleaned source.

## [0.1.2] — 2026-05-27

### Fixed
- **Personal path leakage cleanup** (per CONSTITUTION C1):
  - `docs/manual/INTEGRATION.md` no longer references `D:\AgentWorkspace\`;
    capture scripts now described as auto-locating via `$PSScriptRoot`.
  - All five `docs/manual/scripts/*.ps1` (`make_testcard`, `testcard_viewer`,
    `testcard_viewer_fs`, `capture_all`, `capture_ui`) replaced hardcoded
    `D:\AgentWorkspace\Tubelight\...` defaults with `$PSScriptRoot`-relative
    auto-detection (repo root resolved as `..\..\..` from `scripts/`).
  - `NEXT_STEPS.md` "Manual de usuario" section purged of personal paths;
    points to released `docs/manual/` artifacts instead.
- **Removed**: `docs/manual/PENDING.md` (obsolete v0.1.1 planning doc).

### Notes
- Affects only repo cleanliness; no behaviour change in `tubelight.exe`.
- v0.1.1 release zip kept on GitHub for history (0 downloads at supersede
  time); v0.1.2 is the recommended download.

## [0.1.1] — 2026-05-27

### Added
- **Bilingual user manual (ES/EN)** under `docs/manual/`:
  single-file interactive HTML (307 KB) with sidebar + scroll-spy, substring
  search, lang/theme toggles, lightbox, glossary tooltips, frame strips,
  print stylesheet (WCAG AA). Plus printable PDF (1.68 MB), 80-col TXT,
  single-source bilingual JSON (`manual.json`, 15 sections, 25 glossary
  terms). 42 calibrated screenshots anchored to a known testcard window.
- **Help → Open user manual button** in the in-app menu
  (`src/overlay/menu.cpp`): resolves `docs/manual/manual.html` via five
  candidate paths (dev tree, release zip, sibling layouts) and opens with
  `ShellExecuteW`; fallback to the GitHub repository URL if the local file
  is missing.
- **Reproducible capture pipeline** (`docs/manual/scripts/`):
  `make_testcard.ps1` (PowerShell + System.Drawing generates the
  reference 1280×960 test pattern), `testcard_viewer.ps1` (WinForms
  viewer with a known window title for `--overlay-target` to anchor on),
  `testcard_viewer_fs.ps1` (fullscreen variant for mode-02 demo),
  `capture_all.ps1` (drives the profile/signal/fine galleries),
  `capture_ui.ps1` (drives menu + HUD + mode shots).
- **Manual generators** (`docs/manual/`):
  `build_manual.mjs` (TXT + HTML from JSON, glossary tooltip injection,
  ANSI terminal renderer), `build_pdf.mjs` (Playwright A4 print, requires
  `npm i playwright + chromium`), `validate-manual.mjs` (CI gate: bilingual
  completeness, screenshot files exist on disk, alt non-empty, code blocks
  have `lang`, git_sha present in meta).
- `docs/manual/INTEGRATION.md` documenting the menu integration.

### Changed
- **License**: MIT → PolyForm Noncommercial 1.0.0. Author:
  Alonso J. Núñez (GS·RUN). Commercial contact: `gsrun.editor@gmail.com`.
- `src/main.cpp` and `src/overlay/menu.cpp` bumped from "0.1.0-alpha" to
  "0.1.1" in user-facing version strings.
- `.gitignore` excludes `docs/manual/node_modules/` and
  `docs/manual/package-lock.json` (regenerable via `npm i` in that dir).

### Fixed
- `src/overlay/menu.cpp` `WIN32_LEAN_AND_MEAN` guarded redefinition
  silences MSVC C4005 warning when the macro is already defined on the
  command line.

## [0.1.0-alpha] — 2026-05-26

First development build. Not for distribution. Project bootstraps the full
8-pass shader pipeline architecture, profile system with cited physical
parameters, and platform-specific injection scaffolding.

### Added
- **Project foundation (F1)**
  - CMake build with vcpkg manifest (Windows) and system-package install
    (Linux). MIT license, GitHub Actions CI matrix (Windows MSVC, Linux
    gcc-13 + clang-18).
  - Constitution C1 enforced by `tools/precommit_check_paths.{sh,ps1}` —
    no absolute personal paths in versioned files.
  - 9 spec-forge artefacts under `specs/` (SPEC + PLAN + CONSTITUTION locked).
- **Pipeline core (F2)**
  - 8 shader passes: signal modeling, dithering analysis + reconstruction,
    beam + scanlines (CRT gamma 2.5 linearization), 6 mask types
    (shadow / aperture grille / slot / diamond / cgwg-mix / dot-trio),
    bloom + per-channel halation, temporal placeholder, final composition
    with barrel + vignette + magnetic interference + convergence.
  - `tubelight --shader-only IMG` mode for live preview with key bindings
    1..8 to toggle individual passes.
- **Profiles (F3)**
  - 15 CRT profiles: PVM-8220, PVM-20M4, BVM-20F1U, Commodore 1084S,
    Sharp X68000 CZ-602D + CZ-603D, Sharp CZ-614D, Wells-Gardner K7000,
    NEC MultiSync I + 4FG, Sony GDM-FW900, P31 + P3-amber terminals,
    vintage B&W P4 TV, generic-pvm baseline.
  - 7 signal profiles: RF, Composite NTSC + PAL, S-Video, SCART RGB,
    Component YPbPr, RGB/VGA.
  - Every physical parameter carries a `source.url` + `retrieved_at` per
    Constitution C2. NEEDS-MEASUREMENT items explicitly flagged.
  - JSON Schemas in `schemas/`, C++ validator in `src/profile/`.
  - `tubelight --validate-profile <path>` CLI.
- **Signal modeling (F4)**
  - Pass −1 implements YIQ-space bandwidth-limited Gaussian blur per
    channel, dot crawl beat, ringing, RF ghosting, line / pixel / rf noise.
    Connection-aware: rgb_vga is passthrough, scart_rgb / component skip
    chroma blur, composite / svideo apply per-spec limits.
  - Pass 0 detects 1-pixel-alternating vertical or horizontal dithering
    patterns (mask in alpha).
  - Pass 1 reconstructs dithered patterns by averaging neighbours where
    the mask is high — fixes Sonic Green Hill cascades on composite paths.
  - Pipeline `apply_crt_profile()` + `apply_signal_profile()` map JSON
    fields to GlobalParams; monochrome phosphors auto-zero mask strength.
- **Injection scaffolding (F5)**
  - Linux: `libtubelight_preload.so` intercepts `glXSwapBuffers` + `eglSwapBuffers`
    via `LD_PRELOAD` + `dlsym(RTLD_NEXT, ...)`. Passthrough + log today.
  - Windows: `tubelight_backend.dll` with MinHook trampolines on
    `IDXGISwapChain::Present` (DX11) installed from a worker thread
    spawned by `DllMain`. `tubelight_inject.exe` performs
    `OpenProcess` + `VirtualAllocEx` + `CreateRemoteThread(LoadLibraryA)`
    injection by PID.
  - IPC abstraction: named pipe (`\\.\pipe\tubelight-<endpoint>`) on Windows,
    Unix socket (`$XDG_RUNTIME_DIR/tubelight-<endpoint>.sock`) on Linux,
    newline-delimited JSON wire format per `specs/CONTRACTS.md` §C1.
- **Vulkan + DX12 + PipeWire (F6)**
  - `VK_LAYER_tubelight_overlay` cross-platform Vulkan layer implementing
    the modern `vkNegotiateLoaderLayerInterfaceVersion` entry, full
    instance + device chain, hooked `vkQueuePresentKHR`. Manifest JSON
    ships next to the binary for `VK_ADD_LAYER_PATH` discovery.
  - DX12 hook: `tubelight_backend.dll` extended with `IDXGISwapChain3::Present`
    and `Present1` trampolines via MinHook.
  - PipeWire screencast fallback (Linux): API surface complete; D-Bus
    portal session deferred to v1.1.
- **Polish + release prep (F7)**
  - 5 additional profiles (BVM-20F1U, CZ-603D, MultiSync 4FG, FW900,
    generic-pvm baseline).
  - Pass 6 adds magnetic interference (slow sin-based UV jitter), warm-up
    curve (180 s linear brightness + white-point ramp), and convergence
    offset that grows toward corners.
  - `tubelight --export-slangp <out>` exports the active CRT + signal
    profile pair as a RetroArch slang preset.
  - CI smoke-tests both the .slangp export and bundled-profile validation
    on every push.

### Not implemented yet
- Pass 5 history-FBO temporal (per-channel exponential decay) — v1.1
- PipeWire portal D-Bus integration — v1.1
- ImGui control panel — v1.1
- Installer artifacts (NSIS / AppImage / Flatpak) — packaging in v1.0 RC
- Audio CRT (transformer whine, degaussing thump) — v2.0
- HDR / 10-bit output paths — v2.0
- ARM64 builds — v2.0+
- Vector display mode (Asteroids / Tempest) — v2.0+

[Unreleased]: https://github.com/gs-run/tubelight/compare/v0.1.0-alpha...HEAD
[0.1.0-alpha]: https://github.com/gs-run/tubelight/releases/tag/v0.1.0-alpha
