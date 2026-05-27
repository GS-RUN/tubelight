# Tubelight v0.1.1

**Date**: 2026-05-27
**Author**: Alonso J. Núñez (GS·RUN)
**License**: PolyForm Noncommercial 1.0.0 — commercial: `gsrun.editor@gmail.com`
**Status**: PRIVATE release (repo `GS-RUN/tubelight` not yet public)

## Highlights

- **Bilingual user manual (ES/EN)** under `docs/manual/`: 307 KB single-file
  interactive HTML, 1.68 MB printable PDF, 80-col plain TXT, single-source
  bilingual JSON (15 sections, 25 glossary terms). 42 calibrated
  screenshots, every CRT and signal profile cited against its service
  manual or equivalent primary source.
- **Help → Open user manual button** integrated into the in-app menu;
  the manual opens in your default browser from a layout-aware path
  resolver with GitHub fallback.
- **Reproducible capture pipeline**: PowerShell scripts that drive
  `tubelight.exe` over a known WinForms test card, anchor via
  `--overlay-target`, and produce all 42 PNGs deterministically.
- **License switch** to PolyForm Noncommercial 1.0.0 (commercial contact
  in metadata).

## Manual at a glance

`docs/manual/manual.html` (single-file, no CDN, WCAG AA):

- Sidebar with scroll-spy, MiniSearch-style substring search, ES/EN toggle,
  dark/light theme, lightbox, frame-strip galleries, glossary tooltips.
- Print stylesheet (page-break before each section, hides chrome).
- 15 sections: Welcome · Install · UI tour · First overlay · 4 overlay
  modes · 16 CRT profiles with cited specs · 7 signal profiles · 8
  fine-tune anatomies · Capture & recording (PNG / MP4 / recordable mode
  + WDA/Magnification deep dive) · Audio · Shortcuts table · 5 use-case
  recipes · 8 troubleshooting flows · Glossary · Credits.

## Distribution layout

```
tubelight-0.1.1-win64.zip
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
│       ├── INTEGRATION.md
│       └── assets/  (42 PNGs ~42 MB)
├── CHANGELOG.md
├── LICENSE
└── README.md
```

## Known limitations carried over

- Pass 5 temporal persistence shader is identity; real implementation
  scheduled for v1.1.
- Magnification API source rect is hardcoded to monitor rect — does not
  follow runtime `Ctrl+Alt+T` target changes. Use the CLI `--overlay-target
  <title>` flag for reliable anchoring.
- Synthetic mouse clicks on ImGui tab bars over `WS_EX_LAYERED` windows
  don't switch tabs reliably; the per-tab menu screenshots were captured
  manually by a human operator.

## Verified against historical hardware

Every CRT profile bundle is validated against a primary source: crtdatabase,
ManualsLib, archive.org Commodore 1084S-D1 Service Manual, gamesx.com X68000
wiki, NEC MultiSync 4FG Service Manual, Wikipedia EIA Phosphor Designations
table. URLs and `retrieved_at` dates live in each `profiles/crts/<id>.json`.
