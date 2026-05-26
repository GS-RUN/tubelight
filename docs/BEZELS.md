# Bezels

Tubelight v1.0 ships with **programmatic SDF bezels** rendered entirely in
the Pass 6 composition shader — no external image assets, zero licensing
worries, scale-clean at any resolution. Five styles cover the bundled
profile categories:

| Style id | Look | Auto-picked for |
|----------|------|-----------------|
| 0 — none              | Plain black bars (legacy behaviour) | `--bezel-style 0` only |
| 1 — pvm metal black   | Matte black metal, recessed screen well | PVM-8220, BVM-20F1U, FW900, etc. |
| 2 — beige plastic     | Cream / beige terminal plastic | terminal-p31, terminal-p3-amber |
| 3 — wood console      | Warm wood tone, thick frame | tv-bw-p4 (1960s-70s console TV) |
| 4 — compact Mac       | White plastic, light sheen | mac-classic-white, apple-lisa |
| 5 — generic dark      | Dark grey | commodore-1084s, nec-multisync, others |

The style is picked automatically from the active profile's id / phosphor
type when you load it. To override at runtime, pop the menu (Ctrl+Alt+M),
go to **Composition → Bezel style** and pick a different one. The setting
follows the live params so it's included when you "Save current as
preset…".

## Why SDF instead of PNG?

- **Zero asset licensing**. The bundled PVM / Sony / Apple hardware
  images are all under restrictive IP. Distributing PNGs of them in this
  repo would be a legal headache; rendering an inspired-by-style frame
  in code is not.
- **Resolution-independent**. SDF bezels stay sharp from 800×600 to a
  4K monitor in fullscreen mode. PNGs would either pixelate or need
  multiple resolutions shipped.
- **Per-profile selection by id**. New monochrome profiles you save get
  the beige terminal frame automatically without you having to source a
  matching PNG.
- **Small repo, fast build**. No `assets/bezels/` directory to balloon
  the install.

## Alternative routes if you want photo-real PNGs

If at some point you'd rather have actual photographic bezels for
specific monitors, here are routes that you can wire up *on top* of
the SDF path (the shader code is structured so a PNG sampler could
take precedence when present):

### (b) Mixed — Wikimedia / Flickr CC + SDF fallback

Hardware photos on Wikimedia Commons / Flickr (CC BY-SA) cover several
of the bundled profiles:

| Profile | Likely Wikimedia / Flickr coverage |
|---------|------------------------------------|
| pvm-8220, pvm-20m4, bvm-20f1u | Yes, multiple CC BY-SA photos exist (search "Sony PVM-8220 broadcast monitor") |
| sony-fw900 | Yes — the FW900 has a small cult following with documented photos. |
| commodore-1084s | Yes — Commodore hardware photos are very common in CC repositories. |
| sharp-x68k-cz602d/603d/614d | Sparse. Some JP retro-PC archive sites have photos but licensing isn't always clear. |
| nec-multisync-1 / -4fg | Marketing materials only — typically all-rights-reserved. Hardware photos uncommon. |
| wells-gardner-k7000 | Arcade tube — see MAME artwork below. |
| terminal-p31, terminal-p3-amber | Generic terminal photos exist; the specific tube hardware is unmarked. |
| tv-bw-p4 | Easy — vintage console TVs are well-photographed. |
| mac-classic-white | Apple IP. Photos exist but Apple's logo and trade dress are protected — be careful. |

Workflow:

1. Search the source, prefer images marked CC BY-SA 4.0 or CC BY 4.0.
2. Crop to a square or 4:3 frame with the screen rectangle centred.
3. Cut out the screen area as a transparent mask (PNG with alpha=0
   inside the picture region, alpha=1 outside).
4. Save under `assets/bezels/<profile_id>.png` (this directory doesn't
   exist yet — create it). 1024×768 or 1920×1440 is a sensible size.
5. Wire the sampler in Pass 6: bind the texture, sample at `v_uv` in
   the letterbox region, fall back to the SDF if no texture is bound.

This is mixed mode: PNGs for the profiles you have coverage for, SDF
for everything else.

### (c) The Bezel Project / libretro overlays

The two most comprehensive bezel collections are:

- **The Bezel Project** (<https://thebezelproject.com>) — covers
  hundreds of systems and arcade boards. **The license is restrictive
  and the artwork is not redistributable.** You would have to ask the
  user to download the bezels themselves and point Tubelight at a
  user-supplied directory. Workable but not "out of the box".
- **libretro common-overlays** (<https://github.com/libretro/common-overlays>)
  — generic "PVM-style" overlay PNGs used by RetroArch. Licences are
  mixed (mostly CC BY-SA, some all-rights-reserved). A subset is
  cleanly redistributable. Could be vendored under `assets/bezels/`
  for the styles where licensing is unambiguous.

### (d) MAME artwork

Specifically for `wells-gardner-k7000` (and any future arcade tube
profiles), <https://github.com/MAME-artwork/artwork> ships per-game
bezel art. Most files are CC. Useful if you ever add per-game arcade
profiles. Not relevant to most current bundled profiles.

### Implementation hooks (for the future)

If/when we switch the default to PNG-backed bezels, the Pass 6 shader
already has the picture-rect math (`picture_min`, `picture_max`) to
sample a per-profile bezel texture at the bezel UV. The wire-up would
be roughly:

1. Add `bezel_texture_path` to the `CRTProfile` struct + JSON schema.
2. Load PNG via `stb_image` into a `tubelight::Texture2D`, bind to a
   new sampler `u_bezel_tex` in Pass 6.
3. In the bezel branch, sample `u_bezel_tex` at `v_uv`. If the alpha
   says "transparent" (i.e. screen area of the photo), fall back to
   the SDF result. Otherwise use the texture's RGB.

For now we stay on path (a). Move to (b)/(c) only when you've sourced
images with confirmed licensing.
