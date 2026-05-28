# Risks — Phase 3c (Tubelight)

> Estructura: probabilidad / impacto / detección temprana / mitigación /
> circuit breaker. Ordenado por riesgo agregado descendente.

## R3c-1 — SPIRV-Cross emite HLSL que dxc rechaza

**Probabilidad**: **med**.
**Impacto**: **high** (bloquea F3c-1).

Algunas construcciones GLSL traducidas a HLSL no son válidas en SM 6.0
(ej.: `texture(sampler, vec2)` traduce a `tex.Sample(samp, uv)` que
requiere combined sampler en SM 5.0 pero separated en SM 6.0).

**Detección temprana**: T1.5 (build determinism) falla con error de
compilación dxc en lugar de hash mismatch. Log mostrará HLSL ofensivo.

**Mitigación**:
1. Pasar a SPIRV-Cross la flag `--separate-image-samplers` para forzar
   separated samplers en HLSL (compatible SM 6.0).
2. Si una pasada tiene una construcción intratable, marcar
   `#ifdef VULKAN` / `#ifdef HLSL` en el GLSL y proveer variant manual
   — viola C3c-1 pero está acotado al shader específico, con TODO de
   borrar cuando SPIRV-Cross lo soporte.

**Circuit breaker**: si > 2 shaders requieren variant manual, parar
F3c-1 y considerar adelantar Slang (Phase 7a). 2 días de sunk cost,
re-spec antes de continuar.

---

## R3c-2 — Diferencias de precisión float entre drivers rompen M1

**Probabilidad**: **med**.
**Impacto**: **med**.

NVIDIA y AMD pueden redondear `pow(x, 2.5)` diferentemente. Si DX12
runtime usa el path FMA del DXC mientras GL hace fmul+fadd separados,
el último bit difiere → acumulación a lo largo de 8 pasadas puede
desbordar el umbral 4/255.

**Detección temprana**: TC4.1 falla en CI; PSNR baja a 35-38 dB.

**Mitigación**:
1. Subir el umbral a 5-6/255 PSNR ≥ 38 dB tras validar con review
   manual que la diferencia es invisible al ojo.
2. Identificar la pasada culpable con suite 6, ajustar el GLSL para
   evitar dependencias frágiles (ej.: `pow(x, vec3(2.5))` vs
   `vec3(pow(x.r, 2.5), pow(x.g, 2.5), pow(x.b, 2.5))`).

**Circuit breaker**: si > 8/255 max diff sostenido, hay un bug en la
implementación DX12, no float jitter. Pausar release, debug.

---

## R3c-3 — Constant buffer layout drift entre C++ y HLSL

**Probabilidad**: **med-high**.
**Impacto**: **high** (uniforms basura → render basura).

HLSL impone reglas de packing distintas a C++ default. Un `vec3 a;
float b;` ocupa 16 bytes en HLSL, igual que C++ con `#pragma pack(16)`.
Pero `vec3 a; vec3 b;` ocupa 32 en HLSL (el segundo `vec3` salta a
nuevo 16-byte slot) y solo 24 en C++ sin padding.

**Detección temprana**: D3D12 debug layer warn `D3D12_MESSAGE_ID_CREATE_SHADER`
o pixel basura inmediato en TC4.1.

**Mitigación**:
1. `static_assert(sizeof(PassUniforms_N) == <expected>)` en C++ con
   el valor que reporte SPIRV-Cross al reflectar el cbuffer (sale en
   los .hlsl generados, comment `// cbuffer ... size: N`).
2. D3D12Backend hace reflection del DXIL en `create_pass` y verifica
   que `sizeof == reflection.cbufferSize`. Assert si no coinciden.

**Circuit breaker**: si el problema reaparece tras la mitigación,
generar los structs C++ desde el reflection (auto). Sustancialmente
más complejo; difiérelo a Phase 7a (Slang) si pasa.

---

## R3c-4 — `--screenshot` flag necesita warmup para estado estable

**Probabilidad**: **med**.
**Impacto**: **low-med**.

Pass 5 (temporal persistence) y Pass 4 (bloom con voltage bloom) son
estado-acumulativo. Capturar frame 1 produce output diferente a frame
60.

**Detección temprana**: TC4.1 falla con PSNR oscilante entre runs (no
determinista).

**Mitigación**:
1. `--screenshot <path>` renderiza 60 frames antes de capturar (warmup).
2. En el ciclo de warmup, `set_time(t)` con `t` constante (no incrementa)
   para evitar deriva del noise pass.

**Circuit breaker**: si 60 frames no basta, subir a 120 y documentar
en el script.

---

## R3c-5 — Determinismo dxc roto entre versiones vcpkg

**Probabilidad**: **low-med**.
**Impacto**: **med**.

vcpkg bumpea `directx-shader-compiler` y el nuevo dxc emite DXIL
distinto bit-a-bit. M3 (T1.5) verde antes del bump, rojo después. No
es un bug — es por diseño de dxc — pero rompe el contrato.

**Detección temprana**: T1.5 (3 builds clean) falla justo después de
`vcpkg upgrade`.

**Mitigación**:
1. Pin `directx-shader-compiler` a una versión específica en
   `vcpkg.json` con `version-string`. Documentar bump deliberado en
   CHANGELOG.
2. Cambiar M3 a "hash estable DENTRO de la misma versión de dxc"
   (más débil pero realista).

**Circuit breaker**: si dxc rompe determinismo en parche menor, abrir
issue upstream y considerar fxc fallback para SM 5.0 (peor pero
estable).

---

## R3c-6 — Refactor Pipeline rompe rendering GL silenciosamente

**Probabilidad**: **med**.
**Impacto**: **high** (M2 falla, regresión visible para usuarios v0.1.7).

El switch sobre `pass_index` en `apply_uniforms_for_pass` tiene 8
ramas con uniforms muy específicos. Al portarlo a `set_uniform_block`
es fácil olvidar un campo, equivocar el orden o aplicar el struct
equivocado al pass equivocado.

**Detección temprana**: T3.7 falla — el hash post-refactor difiere del
baseline.

**Mitigación**:
1. Tener TC3.1 (capturar baseline) ANTES de empezar el refactor de
   pipeline.cpp. Sin baseline, no hay forma de detectar regresión.
2. Refactor incremental: una pasada cada vez, capturar diff parcial,
   smoke. Si N pasadas migran y la N+1 rompe, el bisect es trivial.

**Circuit breaker**: si tras 3 intentos de fix la regresión persiste,
revertir Pipeline a estado pre-refactor y reabrir la fase con un nuevo
spec (puede que la abstracción sea incorrecta).

---

## R3c-7 — D3D12 debug layer no disponible en CI runner

**Probabilidad**: **low**.
**Impacto**: **low**.

`D3D12GetDebugInterface` falla en máquinas sin "Graphics Tools"
optional feature de Windows. Reduce calidad de validación pero no
rompe runtime.

**Detección temprana**: log warning `[tubelight][d3d12] debug layer
requested but not available` en CI.

**Mitigación**: documentado, `init` continúa sin debug layer. Para CI
podemos instalar Graphics Tools en el setup step.

**Circuit breaker**: ninguno necesario — es informativo, no crítico.

---

## R3c-8 — Scope creep "ya que estamos, async compute"

**Probabilidad**: **med-high** (mi propia tentación).
**Impacto**: **high** (delay v0.2.0-beta indefinidamente).

VRS (Variable Rate Shading), async compute pairing, command list
recording paralelo — todo es Phase 6a. Cualquier "ya que tocamos
D3D12, esto sería 1 día más"...

**Detección temprana**: cualquier task que no está en TASKS.md → red
flag.

**Mitigación**: el SPEC declara N1-N7 explícitamente. Si surge
tentación, añadir item a `docs/adr/0002` §"Phase 6a queue" y NO
implementar.

**Circuit breaker**: si he añadido > 3 features fuera de TASKS.md a
mitad de fase, reset al último commit que sí estaba en spec.
Aprendido la lección difícilmente en Phase 2c del debrief 2026-05-27.

---

## R3c-9 — Test pixel-equivalence no estable (flaky)

**Probabilidad**: **low-med**.
**Impacto**: **med**.

Captura `--screenshot` puede chocar con DWM compositor: timing del
DwmFlush, drivers que insertan post-process desktop, etc.

**Detección temprana**: 3 runs CI consecutivos con resultados PSNR
diferentes para mismo input.

**Mitigación**: capturar con `glReadPixels` / `Map` del backbuffer
ANTES del present, no del window via GetDC. Garantiza pixels limpios
sin compositor.

**Circuit breaker**: si flakiness persiste tras mitigación, marcar el
test `allow_failure: true` en CI con TODO bloqueante de Phase 3d.
