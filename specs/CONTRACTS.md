# Contracts — Tubelight

## C1 — IPC UI ↔ Backend

**Transport**: named pipe `\\.\pipe\tubelight-<pid>` (Windows) / Unix domain socket `$XDG_RUNTIME_DIR/tubelight-<pid>.sock` (Linux).

**Wire format**: JSON line-delimited (`\n` separator), UTF-8.

### Mensaje: UI → Backend

```json
{
  "cmd": "attach" | "detach" | "set_profile" | "set_signal" | "set_param" | "screenshot" | "shutdown",
  "args": { ... }
}
```

Variantes de `args`:
- `attach`: `{ "target_pid": 12345, "api_hint": "auto" | "dx11" | "dx12" | "opengl" | "vulkan" }`
- `set_profile`: `{ "profile_id": "pvm-8220" }`
- `set_signal`: `{ "connection": "composite_ntsc", "cable_quality": 0.7 }`
- `set_param`: `{ "key": "scanline_strength", "value": 0.85 }`
- `screenshot`: `{ "out_path": "..." }` (path relativo a workdir)
- `shutdown`: `{}`

### Mensaje: Backend → UI (status + events)

```json
{
  "event": "hooked" | "fallback" | "frame_stats" | "error" | "detached",
  "data": { ... }
}
```

Variantes de `data`:
- `hooked`: `{ "api": "dx11" | "dx12" | "opengl" | "vulkan", "swapchain_format": "BGRA8_UNORM", "size": [w,h] }`
- `fallback`: `{ "reason": "anticheat" | "arch_mismatch" | "api_unsupported" | "hook_failed", "capture_mode": "dxgi" | "pipewire" }`
- `frame_stats`: `{ "fps": 59.94, "added_latency_ms": 1.4, "gpu_ms": 1.1 }` (emitido 1 vez/segundo)
- `error`: `{ "code": "...", "message": "..." }`
- `detached`: `{ "reason": "user" | "target_exit" }`

**Errors enumerated**:
- `E_TARGET_NOT_FOUND` — pid no existe
- `E_TARGET_ACCESS_DENIED` — sin permisos para abrir handle
- `E_API_UNSUPPORTED` — DX9 / Mantle / etc no soportado
- `E_HOOK_FAILED` — MinHook / detours falló
- `E_PROFILE_INVALID` — JSON schema violation
- `E_PIPE_CLOSED` — IPC roto

**Invariantes**:
- Backend nunca cierra el target; sólo se desadjunta limpiamente.
- UI no asume estado: tras `attach` debe recibir `hooked` o `fallback` antes de enviar comandos de parámetros.

## C2 — Schema CRTProfile (profiles/*.json)

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
      "retrieved_at": "2026-05-26",
      "notes": "P22 blue chromaticity NEEDS-VERIFICATION — value taken from SMPTE-C reference"
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

**Invariantes**:
- `dot_pitch_mm > 0`
- `phosphor.decay_ms.{r,g,b}` todos ≥ 0
- `beam.scanline_strength ∈ [0, 1]`
- `source.url` no vacío para cada bloque con número físico
- `source.retrieved_at` formato ISO 8601 fecha

## C3 — Schema SignalProfile (profiles/signals/*.json)

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

**Invariantes**:
- `bandwidth.luma_mhz > bandwidth.chroma_{i,q}_mhz` para conexiones moduladas
- `connection ∈ { "rf", "composite", "svideo", "scart_rgb", "component", "rgb_vga" }`
- `standard ∈ { "ntsc_m", "pal_bg", "pal_n", "secam" }`

## C4 — CLI flags

```
tubelight [--target <pid|exe-name>] [--profile <id>] [--signal <id>]
          [--api <auto|dx11|dx12|opengl|vulkan>]
          [--fallback <auto|always>]
          [--headless]
          [--screenshot <path>]
          [--export-slangp <path>]
          [--version] [--help]
```

**Códigos de salida**:
- `0` — OK
- `1` — argumentos inválidos
- `2` — target no encontrado
- `3` — perfil inválido
- `4` — hook falló y fallback no disponible

## C5 — Formato `.slangp` export

Subset compatible con el loader de RetroArch slang-shaders. Permite usar Tubelight presets dentro de RetroArch como fallback.

Detalles del formato: https://github.com/libretro/slang-shaders/blob/master/docs/preset_spec.md

**Invariante**: el export reproduce visualmente el mismo perfil con tolerancia ε≤4/255 por canal.

## Invariantes globales

- **I1** — Tubelight nunca escribe ni lee memoria del proceso target fuera del swap chain hook.
- **I2** — Backend nunca aborta el proceso target. Si el pipeline crashea, restaura el `Present()` original y se desadjunta.
- **I3** — Un perfil cuyo JSON viola el schema produce error temprano en UI; nunca se intenta cargar parcialmente.
- **I4** — Cualquier shader del pipeline puede desactivarse individualmente (debug) sin que la cadena rompa; passes desactivados pasan textura como identity.
- **I5** — Cero red. Tubelight no abre sockets a internet ni telemetría.
