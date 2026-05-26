# Next Steps — for the user

Repo: **https://github.com/GS-RUN/tubelight** — tagged `v0.1.0-alpha`, HEAD on `main` builds & runs on Windows.

## ✅ Validated on real hardware (2026-05-26)

The full pipeline was built and executed locally with the testcard:

- **Toolchain**: VS 2022 BuildTools 17.0 + MSVC 19.44.35222 + vcpkg HEAD `7e99dc22` + CMake 4.2.3.
- **Build**: `scripts\build_windows.bat` configures + compiles in ~1.6 min cold (vcpkg dep install) and seconds incremental.
- **Smoke**: `tubelight.exe --version`, `--help`, `--validate-profile profiles/**/*.json` (22 OK), `--export-slangp` all pass.
- **Visual**: `--shader-only testcard.png --profile pvm-8220 --signal composite_ntsc` opens a GLFW 4.5 window with:
  - Pass 6 barrel + vignette (rounded corners with black drop-off).
  - Pass 3 aperture-grille vertical lines from the PVM-8220 Trinitron mask.
  - Pass −1 composite-NTSC chroma smearing on color transitions.
- 4 profiles warn `NEEDS-MEASUREMENT` for `dot_pitch_mm` exactly as intended for the source-still-unconfirmed entries.

## What works out of the box

- `--validate-profile <path>`
- `--export-slangp <out> --profile <id> --signal <id>`
- `--shader-only <PNG> --profile <id> --signal <id>` (GLFW preview window, keys 1..8 toggle passes, 0 enables all, ESC quits)
- All 15 CRT profiles + 7 signal profiles bundled and citable.

## Pendings honest list (deferred to v1.1)

- **Pass 5 temporal** (history-FBO ping-pong for per-channel phosphor decay + voltage bloom). Currently identity.
- **PipeWire portal D-Bus** session creation in `src/capture/`. Currently returns the documented "not yet implemented" error.
- **ImGui control panel**. CLI is sufficient for v0.1 but the UI is the canonical user-facing surface.
- **M1 latency verification** (<2 ms) with AMD FLM on a real RetroArch/mednafen target. Hook bodies are passthrough today — no perf measurement done.
- **Cross-platform parity (M7)**: Windows build verified; Linux build not yet run.
- **Aspect-ratio handling** in `run_shader_only`: the testcard renders stretched to the 1280×960 window; we should letterbox to the source PAR.
- **Less aggressive default barrel** so corners don't crop visibly until the user opts in.
- **AppImage / NSIS installer** build + smoke test in CI.

## Suggested next sessions

1. **Verify on Linux**: `cmake --preset linux-ninja && cmake --build build/linux-ninja`. CI will already do this on push, but a local run confirms M7 baseline.
2. **Capture a Sonic Green Hill cascade frame** (300×224 RGB extracted from a Mega Drive ROM), run `--shader-only frame.png --profile generic-pvm --signal composite_ntsc`. Confirm the waterfall fuses into water (closes risks R3 + R9 in `specs/RISKS.md`).
3. **Implement Pass 5 history-FBO** (`Pipeline::history_fbo_ping_pong()`), wire `pass5_temporal.frag` to sample the previous frame's Pass 5 output with per-channel decay constants from `CRTProfile::decay_ms_{r,g,b}`.
4. **Cap barrel + add letterboxing** so the default look is conservative (only enable strong barrel when the user explicitly increases `params().barrel_strength`).
5. **Wire ImGui** for live A/B between profiles without restarting.

## Build references

- Windows: `scripts\build_windows.bat` (auto-locates vcvars + vcpkg).
- Linux: `cmake --preset linux-ninja && cmake --build build/linux-ninja` after the apt/dnf/pacman deps in `docs/USER_GUIDE.md`.
- CI (`.github/workflows/ci.yml`): Windows MSVC + vcpkg, Linux gcc-13, Linux clang-18; every push runs path-check, builds, smoke-tests, validates profiles and exports a `.slangp`.
