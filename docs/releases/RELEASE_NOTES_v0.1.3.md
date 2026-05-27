# Tubelight v0.1.3

**Date**: 2026-05-27
**Author**: Alonso J. Núñez (GS·RUN)
**License**: PolyForm Noncommercial 1.0.0 — commercial: `gsrun.editor@gmail.com`
**Repo**: https://github.com/GS-RUN/tubelight (now public)

## Highlights

This is the first **public** release of Tubelight. The user manual has
been swept clean of any "private repo" wording and now points to the
public Releases and Issues pages.

## Changes

### Changed
- `docs/manual/manual.json` §2 download instructions, §13 troubleshooting,
  and §15 credits no longer reference a private repo. The §2 example
  filename was upgraded from a fixed `tubelight-0.1.0-win64.zip` to
  `tubelight-<version>-win64.zip` so future bumps don't desync.
- `NEXT_STEPS.md`: removed the `(PRIVATE)` tag from the repo line.
- `manual.json` `meta.version` / `meta.git_tag` bumped to `0.1.3`.

### Generated
- `manual.html`, `manual.pdf`, `manual.es.txt`, `manual.en.txt`
  regenerated from the cleaned single source.

## Distribution

```
tubelight-0.1.3-win64.zip
├── tubelight.exe
├── epoxy-0.dll
├── glfw3.dll
├── profiles/         (16 CRT + 7 signal)
├── docs/manual/      (HTML + PDF + TXT + INTEGRATION.md + 42 PNG assets)
├── CHANGELOG.md
├── LICENSE
└── README.md
```

## What is Tubelight?

A high-fidelity CRT overlay for Windows. Place a real Win32 window over
any application, and the area underneath is processed by an 8-pass
OpenGL pipeline that emulates 16 historical monitors (Sony PVM-8220,
BVM-20F1U, GDM-FW900, Commodore 1084S, Wells-Gardner K7000, X68000
CZ-602D, IBM 5151 amber terminal, Mac Classic 1-bit, …) with 7 signal
chains (RF, composite NTSC/PAL, S-Video, SCART RGB, Component, RGB VGA).

Every profile is calibrated against a primary source (service manual,
crtdatabase, ManualsLib, archive.org, gamesx.com). Phosphor chromaticity,
dot pitch, per-channel persistence — all cited inside
`profiles/crts/<id>.json`.

See the bundled `docs/manual/manual.html` for the full user manual in
Spanish and English.
