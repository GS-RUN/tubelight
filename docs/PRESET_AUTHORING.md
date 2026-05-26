# Authoring CRT and Signal Profiles for Tubelight

> Disponible desde fase **F3**. Esta es la convención que cualquier perfil JSON (interno o contribuido externamente) debe cumplir.

## Visión general

Tubelight consume dos tipos de perfiles JSON:

- **CRTProfile** (`profiles/crts/*.json`) — un dispositivo histórico real (PVM, Trinitron consumer, X68k monitor, terminal P31, etc).
- **SignalProfile** (`profiles/signals/*.json`) — una conexión analógica entre fuente y tubo (RF / Composite NTSC / S-Video / SCART RGB / Component / RGB-VGA).

Ambos están definidos por JSON Schemas en `schemas/` y validados por `tubelight --validate-profile <path>`.

## Regla cardinal: citas obligatorias (Constitution C2)

Cada número físico (dot pitch, decay de fósforo, bandwidth, h_freq, dot crawl strength) **debe** llevar su `source` con `url` y `retrieved_at`. Un perfil sin citas se rechaza:

```bash
tubelight --validate-profile profiles/crts/my-monitor.json
# E_PROFILE_INVALID: tube.dot_pitch_mm has no source.url
```

Si no encuentras una fuente para un dato concreto:

- Marca el campo con valor aproximado y añade `notes` explicando.
- Cita la mejor fuente que tengas, aunque sea secundaria (foro, fotografía con regla, blog técnico).
- Si no hay forma, marca el campo como `null` y añade en `notes` `"NEEDS-MEASUREMENT"` — el perfil cargará pero el validador emitirá warning.

## CRTProfile completo (ejemplo)

```json
{
  "$schema": "../schemas/crt_profile.schema.json",
  "id": "pvm-8220",
  "display_name": "Sony PVM-8220 (8\" Trinitron)",
  "era": "1985",

  "tube": {
    "mask_type": "aperture_grille",
    "dot_pitch_mm": 0.50,
    "screen_curvature": "flat",
    "diagonal_inches": 8,
    "aspect_native": "4:3",
    "source": {
      "url": "https://crtdatabase.com/crts/sony/sony-pvm-8220",
      "retrieved_at": "2026-05-26"
    }
  },

  "phosphor": {
    "type": "P22",
    "decay_ms": { "r": 1.0, "g": 0.080, "b": 0.080 },
    "chromaticity_smpte_c": {
      "r": [0.63, 0.34],
      "g": [0.31, 0.595],
      "b": [0.155, 0.07]
    },
    "source": {
      "url": "https://crtdatabase.com/faq/phosphor-designations",
      "retrieved_at": "2026-05-26"
    }
  },

  "beam": {
    "focus": 0.85,
    "intensity_curve": "gauss",
    "scanline_strength": 0.75,
    "interlace_mode": "off"
  },

  "glass": {
    "age": 0.0,
    "tint": [1.00, 1.00, 1.00],
    "reflection_strength": 0.05
  },

  "ageing": {
    "phosphor_burn_in": 0.0,
    "purity_drift": 0.0,
    "geometry_warp": 0.0
  },

  "h_freq_khz": 15.734,
  "v_freq_hz": 59.94
}
```

### Campos

| Campo | Tipo | Notas |
|---|---|---|
| `id` | string | kebab-case, único en `profiles/crts/`. Filename = `<id>.json`. |
| `display_name` | string | Mostrado en UI. Incluye marca + modelo + tamaño. |
| `era` | string | Año o década aproximado. |
| `tube.mask_type` | enum | `shadow` / `aperture_grille` / `slot` / `diamond` / `cgwg` / `dot_trio` / `none` |
| `tube.dot_pitch_mm` | number | > 0. Distancia entre fósforos del mismo color. |
| `tube.screen_curvature` | enum | `flat` / `mild` / `aggressive` |
| `tube.aspect_native` | enum | `4:3` / `5:4` / `16:9` |
| `phosphor.type` | enum | `P1` / `P3` / `P4` / `P22` / `P31` / `custom` |
| `phosphor.decay_ms` | object | ms al 10% de brillo por canal R, G, B. Para monocromo, usar mismo valor en los 3. |
| `beam.intensity_curve` | enum | `linear` / `gauss` / `sigmoid` |
| `beam.scanline_strength` | number | [0, 1]. 0 = sin scanlines, 1 = scanlines pronunciadas. |
| `glass.age` | number | [0, 1]. 0 = nuevo, 1 = muy amarillento. |

## SignalProfile completo (ejemplo)

```json
{
  "$schema": "../../schemas/signal_profile.schema.json",
  "id": "composite_ntsc_consumer",
  "display_name": "Composite NTSC (consumer TV, average cable)",
  "connection": "composite",
  "standard": "ntsc_m",

  "bandwidth": {
    "luma_mhz": 4.2,
    "chroma_i_mhz": 0.5,
    "chroma_q_mhz": 0.5,
    "source": {
      "url": "https://en.wikipedia.org/wiki/NTSC",
      "retrieved_at": "2026-05-26"
    }
  },

  "artifacts": {
    "dot_crawl_strength": 0.7,
    "rainbow_banding": 0.4,
    "ringing_amount": 0.3,
    "ghosting_offset_pixels": 0.0,
    "noise_type": "line",
    "noise_strength": 0.05
  },

  "effective_tvl": 350,
  "subcarrier_mhz": 3.579545,
  "h_freq_khz": 15.734
}
```

## Cómo contribuir un perfil

1. Fork de la repo.
2. Copia el perfil más cercano de `profiles/` y rename a tu `<id>.json`.
3. Edita los valores con tus medidas / fuentes.
4. Valida localmente: `tubelight --validate-profile profiles/crts/<id>.json`.
5. Captura una screenshot del perfil aplicado a la golden image `tests/golden/inputs/nes_mario.png` y adjúntala al PR.
6. PR con título `[profile] <id>: <display_name>` y descripción enumerando las fuentes.

Los mantainers revisan que cada `source.url` resuelve y el dato citado coincide con lo en el JSON. Si la fuente desaparece de internet (link rot), la PR puede pedir mover el dato a `[NEEDS-MEASUREMENT]` o reemplazar la fuente.

## Casos especiales

### Monitor monocromo (P31 verde, P3 ámbar, P4 B&W)

`tube.mask_type = "none"` (no hay máscara de color). El color final del fósforo se determina por `phosphor.type` + `phosphor.chromaticity_smpte_c` que en este caso es un único color (los 3 canales mapean al mismo punto cromático).

### TV B&W con tinte de vidrio amarillento

`phosphor.type = "P4"`, `glass.age = 0.4`, `glass.tint = [1.05, 1.00, 0.92]` (ligero shift cálido).

### PVM/BVM profesional

`tube.mask_type = "aperture_grille"`, `beam.focus ≥ 0.90`, `artifacts` casi nulos (señal RGB pura, sin degradación).

### Vector monitor (Asteroids, Tempest)

Mecanismo distinto — no es raster. Diferido a v2 con su propio tipo `tube.mask_type = "vector"` y pipeline alternativo.
