# Tubelight Shaders

Pipeline 8-pass detallado en [`../specs/DESIGN.md`](../specs/DESIGN.md) §D1.

| Archivo | Pass | Estado | Función |
|---|---|---|---|
| `pass_minus1_signal.frag` | −1 | identity (F2) → real F4 | Modelado de la cadena de señal (RF/Composite/SVideo/SCART/Component/VGA) |
| `pass0_analysis.frag` | 0 | identity (F2) → real F4 | Análisis: detección de dithering + luminancia media del frame |
| `pass1_dither_reconstruct.frag` | 1 | identity (F2) → real F4 | Reconstrucción de patrones de dithering (efecto Sonic cascada) |
| `pass2_beam_scanlines.frag` | 2 | **quality (F2)** | Linearización gamma CRT 2.5 + beam Gaussian + scanlines físicas |
| `pass3_mask.frag` | 3 | **quality (F2)** | Shadow / Aperture Grille / Slot / Diamond / CGWG / Dot Trio |
| `pass4_bloom.frag` | 4 | **quality (F2)** → refinement F7 | Bloom + halación cromática (R/G/B radios distintos) |
| `pass5_temporal.frag` | 5 | identity (F2) → real F7 | Persistencia fósforo por canal + voltage bloom + BFI |
| `pass6_composition.frag` | 6 | **quality (F2)** → refinement F7 | Barrel distortion + vignetting + gamma display + convergencia |

## Convenciones

- Versión GLSL: **450 core**. Sin extensiones.
- Vertex shader compartido en `core::default_fullscreen_vertex_source()` (no requiere VBO, single-triangle technique).
- Cada fragment shader recibe `vec2 v_uv` (rango [0, 1]) e implementa un main() que escribe `o_color`.
- Uniforms estándar disponibles en todos los passes:
  - `sampler2D u_source` — textura de entrada (output del pass anterior, o source del usuario en Pass −1)
  - `vec2 u_resolution` — resolución de salida del pipeline en píxeles
  - `int u_pass_index` — índice del pass (0..7)
- Uniforms específicos del pass: ver `core/pipeline.cpp` función `apply_uniforms_for_pass`.

## Espacio lineal

- **Input** del pipeline: textura sRGB-encoded (display gamma).
- **Pass 2** linealiza al espacio CRT (gamma 2.5).
- **Passes 3-5** operan en espacio lineal.
- **Pass 6** aplica gamma display (~2.2) para sRGB-encoded output.

FBO intermedios usan formato `GL_RGBA16F` para evitar clipping en luminancias > 1.0 durante bloom + halation.

## Toggle individual

Cada pass puede desactivarse en runtime (teclas `1..8` en `--shader-only` mode, según T2.8). Un pass desactivado se trata como identity (la textura pasa al siguiente sin modificar).
