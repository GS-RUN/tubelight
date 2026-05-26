# Next Steps — for the user when they're back

The repo is live at **https://github.com/GS-RUN/tubelight** (public, tagged
`v0.1.0-alpha`).

## ⚠️ One manual step before CI runs

The OAuth token used by `gh` did not have `workflow` scope, so
`.github/workflows/ci.yml` is **on disk locally but not yet pushed**. To
fix in 30 seconds:

```bash
cd D:/AgentWorkspace/Tubelight
gh auth refresh -s workflow
git add .github/workflows/ci.yml
git commit -m "ci: add cross-platform build + path-check + profile-validate + slangp-smoke workflow"
git push
```

After that, GitHub Actions will run on every push (Windows MSVC + vcpkg,
Linux gcc-13, Linux clang-18). The path-check job validates Constitution
C1, the profile-validate step verifies M4 + M5 + M8, the slangp smoke
exports a real preset.

## What was built autonomously (2026-05-26)

- **Specs locked**: `specs/SPEC.LOCKED.md`, `specs/PLAN.LOCKED.md`,
  `specs/CONSTITUTION.LOCKED.md`. 5 NEEDS-INPUT items (NI-1..NI-5) all
  resolved per your decisions.
- **F1**..**F7**: every phase of `PLAN.LOCKED.md` has a commit (see
  `specs/INDEX.md` for the table). 8 commits total, tagged `v0.1.0-alpha`.
- **15 CRTProfile + 7 SignalProfile**: all bundled in `profiles/`, every
  number has `source.url` per Constitution C2.
- **8-pass shader pipeline**: passes 2, 3, 4, 6 are quality
  implementations; Pass −1 is the full signal modeling shader; Pass 0/1
  do dithering detection + reconstruction (Sonic cascade demo path).
- **Injection scaffolding**: LD_PRELOAD .so for Linux, MinHook DX11+DX12
  backend.dll + injector.exe for Windows, cross-platform Vulkan layer.
  IPC via named pipe (Win) and Unix socket (Linux).
- **PipeWire fallback**: API surface in place, D-Bus portal implementation
  deferred (see CHANGELOG "Known limitations").
- **`.slangp` export**: `tubelight --export-slangp out.slangp --profile X
  --signal Y` produces a RetroArch preset.

## What still needs hardware verification

These are the metrics from SPEC that can't be validated without running on
a real target machine + a real emulator:

- **M1** (<2 ms hook latency) — needs AMD FLM or equivalent on Windows,
  Linux equivalent on a GPU machine.
- **M2** (≤16.7 ms fallback) — needs DXGI / PipeWire fallback validation.
- **M6** (60 fps at 4K) — needs perf bench on the target GPU.
- **M7** (Win/Linux parity ε≤2/255) — needs golden frame comparison run on
  identical GPU on both OSes.

## Suggested follow-ups (no specific order)

1. Push `ci.yml` (above).
2. Build locally — install vcpkg + Visual Studio + Vulkan SDK on Windows,
   or apt deps on Linux per `docs/USER_GUIDE.md`. Confirm `tubelight
   --shader-only test.png --profile pvm-8220 --signal composite_ntsc` runs.
3. Capture a Sonic Green Hill frame, run through composite_ntsc, see the
   cascade fuse — that closes the R3 + R9 demo risks in `specs/RISKS.md`.
4. v1.1 polish: Pass 5 history-FBO temporal model, PipeWire D-Bus portal,
   ImGui control panel, M1 latency benchmark on real target.
5. Compare side-by-side against CRT-Royale / Guest-Advanced on the same
   inputs to validate R3 (calidad shader).
