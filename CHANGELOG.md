# Changelog

All notable changes to Tubelight. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [SemVer 2.0](https://semver.org/).

## [Unreleased]

### Known limitations
- Pass 5 (temporal persistence / voltage bloom / BFI) is still an identity
  shader. Real implementation requires a history-FBO refactor scheduled
  for v1.1 — see specs/PLAN.LOCKED.md F7 T7.2.
- PipeWire screencast capture is scaffolding only; D-Bus portal session
  and stream consumption ship in v1.1.
- ImGui control panel is not built; configuration is via CLI flags only.
- M1 (<2 ms hook latency) is unverified — requires a real target run with
  AMD FLM or equivalent. Hook bodies are passthrough today.

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
