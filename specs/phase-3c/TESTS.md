# Tests — Phase 3c (Tubelight)

## Suite 1: shader build determinism (CI Windows)

**Propósito**: demuestra M3 — el flujo glslang → SPIRV-Cross → dxc
genera DXIL estable a lo largo de builds.

**Setup**: máquina CI Windows con vcpkg + MSVC. Trigger: cualquier
push que toque `shaders/` o `cmake/CompileShaders.cmake`.

**Casos**:
- **TC1.1**: 3 builds clean consecutivos (`rm -rf build && cmake
  --preset windows-vcpkg && cmake --build`). Capturar SHA256 de cada
  `.dxil` después de cada build.
- **TC1.2**: Edit trivial en un `.frag` (añadir comment) → build →
  hash cambia. Quitar el comment → build → hash vuelve al original.

**Pass criterion**: TC1.1 los 3 sets de hashes idénticos. TC1.2 hashes
diferentes entre con/sin comentario, idénticos entre dos sin
comentario.

**Tooling**: script bash en `tests/golden/build_determinism.sh`,
invocado desde CI.

---

## Suite 2: GLBackend handles unit test (local + CI)

**Propósito**: demuestra que `IRenderBackend` v2 funciona en GL antes
de que Pipeline dependa de ello (gate de F3c-2).

**Setup**: ejecutable `test_gl_handles` compilado bajo
`TUBELIGHT_BUILD_TESTS=ON`. Crea ventana GLFW oculta.

**Casos**:
- **TC2.1**: crear backend, init, crear texture 256×256, upload
  patrón de gradiente RGBA8, leer back con glReadPixels → match
  byte-exacto.
- **TC2.2**: crear render target 256×256 RGBA16F, bind, clear a
  (0.25, 0.5, 0.75, 1.0), leer back → match con tolerancia float (ε
  < 1/65535).
- **TC2.3**: crear pass simple (passthrough `out = texture(source, uv)`),
  bind, bind_texture slot 0, set_uniform_block (sizeof=16, dummy),
  draw, leer back → output == input texture.
- **TC2.4**: destruir todo, shutdown, sin GL errors.

**Pass criterion**: los 4 TCs verde, `glGetError()` nunca distinto de
`GL_NO_ERROR`.

**Tooling**: simple `main()` que devuelve 0/1, integrado a CTest.

---

## Suite 3: GL regression byte-exact (gate de F3c-3)

**Propósito**: demuestra M2 — el refactor de Pipeline a handles no
cambia el output GL.

**Setup**: baseline capturado ANTES del refactor en v0.2.0-alpha.0:
`tests/golden/gl_v017_baseline.png` (1280×960, pvm-8220 + composite_ntsc,
frame 60). Hash SHA256 commit'eado en `gl_v017_baseline.sha256`.

**Casos**:
- **TC3.1**: tras T3.7, correr el binario actual con los mismos args y
  `--screenshot tests/tmp/gl_post_refactor.png`. Hash SHA256 →
  comparar contra baseline.

**Pass criterion**: hashes idénticos. Si difieren, el refactor cambió
el comportamiento GL — bisect o revert.

**Tooling**: `tests/golden/check_gl_byte_exact.sh`. Llamado manualmente
en F3c-3; opcional en CI (rebuild determinístico GL es frágil entre
drivers, lo dejamos como local-only).

---

## Suite 4: pixel-equivalence GL vs D3D12 (gate de F3c-5, CI Windows)

**Propósito**: demuestra M1 — el output DX12 es perceptualmente igual
al GL.

**Setup**: script PowerShell captura frame 60 de ambos backends sobre
el testcard estándar con 3 profile combos.

**Casos**:
- **TC4.1**: `pvm-8220 + composite_ntsc` (caso primario).
- **TC4.2**: `fw900 + rgb_vga` (HD widescreen, sin signal artifacts).
- **TC4.3**: `terminal-p31 + rgb_vga` (monocromo P31, sin máscara).

Para cada caso: capturar `gl_<combo>.png` y `dx12_<combo>.png`, correr
`dx12_vs_gl_psnr.py` con los dos.

**Pass criterion**:
- TC4.1: PSNR ≥ 40 dB, Δ max per-canal ≤ 4/255.
- TC4.2: PSNR ≥ 42 dB (más limpio, menos pasadas activas), Δ ≤ 3/255.
- TC4.3: PSNR ≥ 45 dB (monochrome path simplifica), Δ ≤ 2/255.

Si TC4.1 pasa pero TC4.2 o TC4.3 fallan: investigar el delta (puede
ser bug aislado en pass específica que activa cierto profile).

**Tooling**:
- `tests/golden/capture_baseline.ps1`: lanza binario, espera, lee
  screenshot, kills.
- `tests/golden/dx12_vs_gl_psnr.py`: Pillow + numpy.
- CI workflow `.github/workflows/pixel_equivalence.yml`.

---

## Suite 5: D3D12 device fallback smoke (gate de F3c-5)

**Propósito**: demuestra R12 sigue cubierto — si DX12 no arranca, GL
recoge el fallback.

**Setup**: simulamos failure forzando `BackendKind::D3D12` cuando el
sistema lo soporta pero forzando un error en `create_swap_chain_resources`
(via flag `--debug-force-dx12-fail` añadido en TASKS si no existe).

**Casos**:
- **TC5.1**: `tubelight --renderer dx12 --shader-only testcard.png`
  en máquina sin DX12 (simulado vía test) → caída controlada con log
  `[tubelight] D3D12 path failed; falling back to OpenGL`. GL renderiza
  el testcard.
- **TC5.2**: en máquina con DX12 OK, `--debug-force-dx12-fail` →
  mismo fallback.

**Pass criterion**: ambos TCs producen output GL válido y log claro.
Exit code 0.

**Tooling**: smoke manual + script PowerShell para CI.

---

## Suite 6: Pipeline orchestration golden (sanity, opcional)

**Propósito**: pasaditas individuales (toggle ON/OFF con `--pass-only N`
si se implementa o con `0`+`1..8` keys en runtime) producen el output
esperado. Útil para depurar cuando una pasada de DX12 difiere de GL.

**Setup**: manual durante F3c-4 debugging. No es gate.

**Casos**: por-pasada, capturar el output con solo esa pasada activa,
comparar GL vs DX12 con PSNR.

**Pass criterion**: cada pasada individual PSNR ≥ 40 dB. Si una falla
aislada, ese DXIL/uniforms tiene un bug específico.

**Tooling**: manual, runtime keys + screenshot.

---

## Suite 7: release zip integrity (gate de F3c-5)

**Propósito**: el zip final shippea los `.dxil` y NO incluye DLLs DXC.

**Setup**: tras `release-publisher` cierra zip.

**Casos**:
- **TC7.1**: unzip → verificar `shaders/dxil/pass_*.dxil` (9 archivos
  incluyendo fullscreen).
- **TC7.2**: unzip → verificar **ausencia** de `dxcompiler.dll`,
  `dxil.dll`.
- **TC7.3**: tamaño zip ≤ 60 MB.

**Pass criterion**: 3/3 TCs.

**Tooling**: script Python en `tests/release/check_zip.py`.
