# Tubelight v0.1.2

**Date**: 2026-05-27
**Author**: Alonso J. Núñez (GS·RUN)
**License**: PolyForm Noncommercial 1.0.0 — commercial: `gsrun.editor@gmail.com`
**Supersedes**: v0.1.1 (0 downloads at time of supersede)

## Highlights

- **Cleanliness patch**: enforced Constitution C1 across versioned files.
  No more `D:\AgentWorkspace\...` personal paths in `docs/manual/`, the
  capture scripts, or `NEXT_STEPS.md`.

## Changes

### Fixed
- `docs/manual/INTEGRATION.md` — removed `D:\AgentWorkspace\Tubelight\`
  reference; capture-script docs now describe `$PSScriptRoot` auto-detect.
- Five PowerShell capture scripts (`docs/manual/scripts/*.ps1`) now
  resolve repo root relatively to their own location via
  `(Resolve-Path (Join-Path $PSScriptRoot "..\..\.."))`. Defaults work
  on any clone, anywhere. Override with `-ExePath` / `-AssetsRoot` /
  `-Image` if needed.
- `NEXT_STEPS.md` — purged the obsolete "manual planning" section that
  contained the personal path.
- Removed `docs/manual/PENDING.md` (obsolete v0.1.1 planning doc).

### Notes
- `tubelight.exe` binary behaviour unchanged — only version string bumped.
- v0.1.1 release kept on GitHub for history. Use v0.1.2.

## Distribution layout

```
tubelight-0.1.2-win64.zip
├── tubelight.exe
├── epoxy-0.dll
├── glfw3.dll
├── profiles/
│   ├── crts/*.json    (16 CRT profiles)
│   └── signals/*.json (7 signal profiles)
├── docs/
│   └── manual/
│       ├── manual.html
│       ├── manual.pdf
│       ├── manual.es.txt
│       ├── manual.en.txt
│       ├── INTEGRATION.md (cleaned)
│       └── assets/  (42 PNGs ~42 MB)
├── CHANGELOG.md
├── LICENSE
└── README.md
```
