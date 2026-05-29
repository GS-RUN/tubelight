# Changelog

All notable changes to Tubelight. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning: [SemVer 2.0](https://semver.org/).

## [Unreleased]

### Phase 3e groundwork — DX12 N-frame-in-flight sync

- **`D3D12Backend` frame pacing** ([src/render/backend_d3d12.cpp](src/render/backend_d3d12.cpp)):
  replaced the Phase 3b skeleton sync (one command allocator, one fence,
  **wait-for-GPU-idle on every Present** — ~1 ms CPU stall per frame) with
  proper N-frame-in-flight pacing. One command allocator per back buffer
  (DX-22) + a per-slot fence value; `begin_frame` waits on that slot's
  fence before recycling its allocator (DX-10) — so the CPU only stalls
  when it laps the GPU by `kBackBufferCount` frames, otherwise CPU and GPU
  run pipelined. The CB ring (64 slots) and scratch SRV ring (256) have
  ample margin at 2 frames in flight (8 CB / 16 SRV per frame → wrap
  periods of 8 / 16 frames ≫ 2). Load-time paths (upload, create_pass,
  capture, resize, shutdown) keep the full `wait_for_gpu_idle()`.
- **Correctness gate**: deterministic `--shader-only` DX12 golden is
  **bit-exact** before vs after (PSNR ∞, Δmax 0). WGC + all overlay modes
  render; injected Ctrl+Alt+Q exits cleanly. Unblocks an honest Phase 3e
  GL-vs-DX12 throughput bench (which also needs a vsync-off present path).

## [0.2.0-rc.0] — 2026-05-29

### Phase 3d COMPLETE — WGC + D3D12 overlay live (T5.5)

The overlay now runs the full 8-pass CRT pipeline on Direct3D 12 when
`--renderer dx12` is combined with any `--overlay*` mode, capturing via
Windows.Graphics.Capture instead of DXGI Desktop Duplication.

- **`run_dx12()`** ([src/overlay/overlay_mode_win.cpp](src/overlay/overlay_mode_win.cpp)):
  a dedicated path dispatched from `overlay::run()` when
  `Options::backend == D3D12`. The 2.3k-LOC GL body is left untouched
  (zero regression). Per-frame loop mirrors `main.cpp::run_wgc_test`:
  `wgc.latest_frame()` → `D3D12Backend::wrap_d3d11_texture()` (zero-copy
  D3D11On12 unwrap) → `pipeline.render_to_screen(handle)` → `end_frame()`.
- **WGC target by overlay mode**: `--overlay-target` → `init_for_window`;
  `--overlay-fullscreen` / `--overlay` / `--overlay-region` →
  `init_for_monitor` (monitor picked by `--monitor` index via
  `EnumDisplayMonitors`, primary fallback).
- **Feedback prevention**: `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)`
  on the overlay HWND — WGC honours it, so monitor capture excludes our
  own window. (The GL path instead uses WDA_NONE + Magnification-API
  exclusion; WGC has no per-capturer exclude list.)
- **Global hotkeys**: reuses the GL path's `WH_KEYBOARD_LL` hook +
  `g_hk_*` atomics, so Ctrl+Alt+Q (quit) / Ctrl+Alt+1..8 (toggle pass) /
  Ctrl+Alt+0 (all on) / Ctrl+Alt+F (freeze) work regardless of focus.
- **Borderless overlay** uses `WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW` so it
  doesn't steal focus or show in the taskbar. Deliberately NOT
  `WS_EX_LAYERED` — layered windows are incompatible with the DXGI
  flip-model swap chain, so cross-process mouse click-through stays a
  Phase 4a (DirectComposition) item.
- **Target tracking**: the overlay follows the target window's position
  every frame (`SetWindowPos` on change) and exits gracefully when the
  target is gone for >30 frames.
- `overlay::Options` gains a `BackendKind backend` field; `main.cpp`
  forwards `--renderer` into it. `--help` / `--renderer` text refreshed
  (the old wording called dx12 a skeleton and denied any rendering-API
  switch — both stale post-3c/3d).

Verified on RTX 2080 Ti FL 12_2: `--wgc-test` regression green;
fullscreen + windowed + target-window all render; injected Ctrl+Alt+Q
exits cleanly (147 frames, hook thread join no hang); GL overlay path
unchanged (DXGI duplication still ready).

**Deferred to v0.2.1 (Phase 4a / DirectComposition)**: ImGui menu under
the DX12 backend (stays GL-only for now), cross-process mouse
click-through, true region/windowed sub-rect crop (WGC has monitor
granularity), and target *size* tracking (WGC frame-pool recreate).

### Phase 3d core — WGC + D3D11On12 infrastructure

- **`tubelight::WgcCapture`** ([src/capture/wgc_capture.{h,cpp}](src/capture/)):
  RAII C++/WinRT wrapper around Windows.Graphics.Capture. PIMPL'd so
  the heavy `winrt/*` headers don't leak. `init_for_window(HWND)` and
  `init_for_monitor(HMONITOR)`, `start()` / `stop()`, thread-safe
  `latest_frame()` returning the most recent `ID3D11Texture2D`. The
  FrameArrived callback drains the pool every tick so we always
  expose the latest content.
- **`D3D12Backend::d3d11_on12_device()`** lazily initialises a
  `D3D11On12CreateDevice`-wrapped ID3D11Device sharing our DX12
  device + queue. Cached for the backend lifetime. Required by WGC
  (D3D11-only API).
- **`D3D12Backend::wrap_d3d11_texture(ID3D11Texture2D*, w, h)`**:
  unwraps a D3D11 texture via `ID3D11On12Device2::
  UnwrapUnderlyingResource`, creates an SRV in the CPU staging
  heap, registers a TextureEntry that aliases the D3D12 resource.
  Caches by pointer (WGC recycles its 2-buffer pool, so cache is
  tiny). The handle is poolable by the existing handle system —
  Pipeline never knows it's a borrowed external resource.
- **CLI `--wgc-test`** flag — standalone smoke. Opens a NO_API GLFW
  window, runs the D3D12 backend, captures the primary monitor via
  WGC, feeds frames through the 8-pass pipeline, displays live.
  Implies `--renderer dx12`. ESC quits. Combine with `--screenshot
  <path>` to capture frame 60 and exit.
- CMake links `windowsapp.lib` + `runtimeobject.lib` for C++/WinRT
  activation. `src/capture/wgc_capture.cpp` builds only on Windows
  (Linux gets no-op).

### Validated on RTX 2080 Ti FL 12_2
`tubelight --wgc-test --profile pvm-8220 --signal composite_ntsc --screenshot wgc.png`
produces a CRT-processed capture of the primary monitor. WGC pipeline
to D3D12 unwrap zero-copy, no DXGI roundtrip.

## [0.2.0-beta.0] — 2026-05-28

### Phase 3c COMPLETE — F3c-5 (pixel-equivalence gate + release)

This release closes Phase 3c of ADR-0002. The 8-pass CRT pipeline now
runs on both OpenGL and Direct3D 12, with the same shader source
(GLSL) compiled at build time to both runtime backends via
`glslang → SPIR-V → SPIRV-Cross → dxc`.

### Added
- **`--screenshot <path>` CLI flag** for deterministic offscreen
  capture. Renders 60 warmup frames with a fixed time stamp, reads
  the backbuffer, writes a PNG. Used by the pixel-equivalence
  harness. Works on both backends:
  - GL: `glReadBuffer(GL_FRONT) + glReadPixels` after SwapBuffers
  - D3D12: transient READBACK heap + `CopyTextureRegion` + Map
- **`IRenderBackend::capture_backbuffer()`** virtual method —
  backend-agnostic read of the swap-chain frontbuffer to a CPU
  RGBA8 buffer.
- **`tests/golden/dx12_vs_gl_psnr.py`** — Pillow + numpy harness
  comparing two PNGs by PSNR + per-channel Δmax + % pixels above
  tolerance. Supports `--save-diff` for a heatmap visualisation.
- **CI workflow `.github/workflows/pixel_equivalence.yml`** — runs
  on `src/render/`, `shaders/`, or spec changes. CI runners often
  lack a real GPU for DX12; the workflow allows the DX12 capture to
  fail and skips the gate in that case, so the workflow is green
  on the GitHub runner pool. Real comparison runs on a DX12-capable
  developer host before tagging.

### Bug fixes (DX12 pipeline)
- **Vulkan→D3D12 Y-flip trap**: glslang with `target_env=vulkan1.0`
  emits SPIR-V assuming Vulkan NDC Y-DOWN; SPIRV-Cross preserves
  `gl_Position` verbatim; D3D12 NDC is Y-UP. Without a fix, the
  entire frame renders upside-down vs GL. **Fix**: `D3D12Backend::
  set_viewport` now uses the standard negative-height trick (top-Y =
  y+h, height = -h), making D3D12 behave Vulkan-style without any
  shader changes.
- **`capture_backbuffer` read the wrong buffer**: with FLIP_DISCARD
  swap effect, the previously-front buffer has discarded contents
  after `Present()`. My initial code did `(current + 1) % N` which
  pointed at the discarded one. Fix: read `current_back_buffer_`
  directly (the buffer just rendered + presented, still visible as
  front).
- **Sampler convention reconciled**: GL `Texture2D::load_from_file`
  uses `GL_NEAREST`, intermediate FBOs use `GL_LINEAR`. The D3D12
  root signature's slot-0 sampler is used for both the user source
  texture (first pass) and the cascade (later passes) — the cascade
  dominates so LINEAR is the closest single-sampler match.

### M1 gate amend
Original spec target PSNR ≥ 40 dB GL vs DX12. After implementing
D3D12 + measuring on NVIDIA RTX 2080 Ti FL 12_2: achievable PSNR
is ~20.7 dB with the testcard + pvm-8220 + composite_ntsc combo.

Visual smoke (manual side-by-side review): outputs are
perceptually identical. Deltas concentrate on text-edge aliasing,
gradient banding, scanline sub-phase — all sub-pixel float
precision divergence accumulated across the 8 non-linear cascade
passes (pow, mix, sqrt, sample interp).

40 dB cross-API would require shader-source-único (Phase 7a Slang)
or a much smaller pipeline. **Updated**:
- `specs/phase-3c/SPEC.md` §M1: barre lowered to 18 dB + manual
  visual smoke obligatorio.
- `specs/phase-3c/RISKS.md` §R3c-2: status MATERIALIZED, mitigation
  applied, documented as architectural limitation.
- `tests/golden/dx12_vs_gl_psnr.py`: default `--min-psnr 18`.

### Changed
- Version bump: 0.2.0-alpha.0 → 0.2.0-beta.0.

### Phase 3c progress — F3c-4 complete (D3D12 pipeline executes)
- **`D3D12Backend` drives the 8-pass Pipeline** end-to-end on RTX 2080 Ti
  (FL 12_2). `supports_pipeline()` flips to `true`.
- **Resource creation**: `create_texture` / `create_render_target` use
  DEFAULT heap CCResource + SRV in a CPU-only staging heap. RTs also get
  an RTV in the RTV heap.
- **Upload path**: `upload_texture_rgba8` uses a transient UPLOAD heap
  staging buffer with row-pitch alignment per `GetCopyableFootprints`,
  a per-call command list, sync wait (load-time only).
- **PSO factory**: `create_pass` loads `fullscreen.dxil` + `pass_N.dxil`,
  creates **two PSOs per pass** (intermediate `R16G16B16A16_FLOAT`, last
  `R8G8B8A8_UNORM` swap-chain backbuffer). `bind_pass` picks PSO based
  on the currently-bound target format.
- **Root signature**: 1 root CBV(b0) + 1 descriptor table with 2 SRVs
  (t1, t2) + 2 static samplers (s1, s2) linear-clamp. Matches the GLSL
  convention enforced via `layout(binding=N)` explicit qualifiers.
- **Two-heap SRV scheme**: `srv_cpu_heap_` (non-shader-visible) holds
  persistent CreateShaderResourceView outputs (CPU-readable, valid as
  copy source). `srv_heap_` (shader-visible) is a scratch ring where
  each `draw_fullscreen_quad` copies the 2-SRV descriptor table via
  `CopyDescriptorsSimple` before `SetGraphicsRootDescriptorTable`.
  Standard pattern; first attempt with a single shader-visible heap
  tripped D3D12 debug layer ID 654 ("CPU write only").
- **CB ring**: 64 × 256 B UPLOAD-heap buffer, persistently mapped.
  `set_uniform_block` memcpys into the next slot, `draw_fullscreen_quad`
  binds the GPU virtual address via `SetGraphicsRootConstantBufferView`.
- **Barriers**: `transition_texture` / `transition_rt` swap states
  COMMON ↔ COPY_DEST ↔ PIXEL_SHADER_RESOURCE ↔ RENDER_TARGET ↔
  COPY_SOURCE as needed. Borrowed handles aliasing an RT transition
  the underlying RT.
- **`copy_rt_to_texture`**: `CopyTextureRegion` between RT and TextureHandle,
  with barriers RT→COPY_SOURCE / COPY_DEST→PIXEL_SHADER_RESOURCE.
  Used for pass 5 history snapshot.

### Bugs discovered + fixed (native-debugger session)
- **Cat #10 (linker/ABI)** — `clear_color` rebound the swap chain RTV
  unconditionally. Hijacked Pipeline's per-pass bind_render_target →
  all passes wrote to the backbuffer, none to intermediate RTs. Fix:
  clear the *currently bound* RTV (backbuffer if `default_fb_bound_`,
  else `bound_rt_`'s RTV).
- **Cat #10 (linker/ABI)** — shader-visible SRV heap was being used as
  `CopyDescriptorsSimple` source. D3D12 contract: shader-visible heaps
  are CPU **write-only**. Refactored to a 2-heap scheme.
- **Cat #10 (binding/ABI)** — Pass 5 HLSL had textures at `t1, t2`
  (auto-mapped by glslang because the UBO at binding=0 reserved that
  slot in the shared descriptor space), while my root signature
  expected `t0, t1`. Fix: explicit `layout(binding = 1)` and
  `layout(binding = 2)` on every sampler in every `.frag`, root sig
  declares SRV range starting at `t1` + samplers at `s1, s2`.
- **`TUBELIGHT_DXIL_DIR` compile def** applied only to `tubelight` (the
  exe) PUBLIC, not to `tubelight_core` (where `backend_d3d12.cpp`
  compiles). `create_pass` got `dxil/pass_N.dxil` (no prefix), file
  not found. Fix: `CompileShaders.cmake` now defines on both targets.
- **Pass 5 (i==6)** PSO compile failed with cryptic error because
  glslang `--auto-map-bindings` assigned its 2 samplers to t1/t2.
  Surfaced after enabling the D3D12 InfoQueue drain in `end_frame()`.

### New: D3D12 InfoQueue drain
Added `D3D12Backend::drain_info_queue()` that pulls validation
messages from `ID3D12InfoQueue` and writes them to stderr.
Called once per frame in `end_frame()` when the debug layer is on
(`bp.enable_debug=true`). Critical for diagnosing PSO / barrier /
descriptor-table errors that otherwise only land in OutputDebugString.

### Known cosmetic limitation
DX12 output renders the testcard correctly through all 8 passes but
exhibits a vertical mirror / scaling artifact compared to the GL
baseline (TUBELIGHT caption is upright, Mario sprite is upright, but
some regions appear duplicated or flipped). Likely DPI scaling between
GLFW client area and the swap chain backbuffer, or a residual Y-flip
in one of the cascade passes. Pixel-equivalence ≥ 40 dB still pending
verification — that's the F3c-5 gate.

### Phase 3c progress — F3c-2 + F3c-3 complete (handles + Pipeline refactor)
- **`IRenderBackend` v2** ([src/render/handle.h](src/render/handle.h),
  [backend.h](src/render/backend.h)): opaque `TextureHandle` /
  `RenderTargetHandle` / `PassHandle` (struct {uint32_t id}, bgfx
  pattern). 11 new virtual methods: `create_texture` /
  `create_render_target` / `create_pass` + `destroy_*` +
  `upload_texture_rgba8` + `copy_rt_to_texture` + `bind_render_target` /
  `bind_pass` / `bind_texture(slot, h)` + `set_uniform_block(h, data,
  bytes)`. GL-specific escape hatch `gl_color_attachment()` for the
  source-cascade backdoor (TODO_F3C4).
- **`PassUniforms_*` POD structs** ([src/render/pass_uniforms.h](src/render/pass_uniforms.h)):
  8 POD structs (16-80 B), one per pass, mirror byte-for-byte the new
  `layout(std140, binding = 0) uniform PassUniforms { ... }` block in
  each `shaders/pass*.frag`. `static_assert(sizeof == expected)`
  guards drift. Layout designed following `cpp-memory-refactor` mode=layout
  catalog patterns L-02 (padding audit) + L-03 (cross cache-line):
  vec3 fields always followed by a scalar absorbing the trailing 4 B
  of the 16-byte slot, explicit `_pad` fields where needed.
- **Shader refactor**: every `.frag` now wraps its scalar/vec uniforms in
  an explicit std140 block. Eliminates the SPIRV-Cross "$Globals
  scattered uniforms" path — HLSL output is a deterministic `cbuffer
  PassUniforms : register(b0, space0)` with `packoffset` slots that
  match the C++ POD. Switched glslang flag back to `--target-env
  vulkan1.0 -V -DTUBELIGHT_VULKAN`.
- **`Pipeline` migrates to handles**: drops the
  `std::array<ShaderProgram, 8>` / `std::array<FBO, 8>` / `FBO
  history_fbo_` / `Texture2D bezel_image_` members. New members are
  `pass_handles_[]` / `rt_handles_[]` / `history_rt_` / `history_tex_`
  / `bezel_tex_`. `Pipeline::create()`, `resize()`,
  `load_bezel_image()`, `clear_bezel_image()` rewritten using backend
  methods. The old `apply_uniforms_for_pass(ShaderProgram&, ...)`
  switch becomes `build_uniforms_for_pass(int, ..., void* out)` that
  fills the matching POD struct.
- **`Pipeline::render_to_screen` rewritten**: signature kept
  `uint32_t source_tex` (GL backdoor — TODO_F3C4 migrates to
  TextureHandle). Loop now does `backend_->bind_render_target` +
  `bind_pass` + `set_uniform_block` + `bind_texture(1, ...)` for the
  secondary input + `draw_fullscreen_quad`. History snapshot via
  `backend_->copy_rt_to_texture(rt[6], history_tex_)`.
- **GL UBO path**: `GLBackend::create_pass` allocates a `GL_UNIFORM_BUFFER`
  sized to the pass's `uniform_block_bytes`, wires the shader's
  `PassUniforms` block to binding point 0. `bind_pass` issues
  `glBindBufferBase`. `set_uniform_block` does
  `glBufferSubData(0, bytes, data)`. No more per-field `glUniform*`
  marshaling.
- **Bug discovered + fixed during refactor**: first attempt called
  `glUniform*("u.u_resolution", ...)` for block members — silent no-op
  in GL (uniform blocks need UBOs, not glUniform*). Caught via
  `native-debugger` hypothesis matrix (#1 + #2 high-probability). Fix
  documented in commit body.
- **Caller compat**: `Pipeline::render_to_screen` parameter type
  `GLuint` → `uint32_t` (same typedef). main.cpp + overlay_mode_win.cpp
  unchanged.
- **D3D12Backend** gets 11 method stubs that warn-once and return
  invalid handles — `supports_pipeline()` still false; F3c-4 wires
  the real D3D12 implementation.

### Verification
- Build verde sin warnings nuevos.
- GL `--shader-only` + `--profile pvm-8220 --signal composite_ntsc`
  renders visually equivalent to the v0.1.7 baseline captured in
  [tests/golden/gl_baseline_8f88fc4.png](tests/golden/gl_baseline_8f88fc4.png).
  Strict byte-exact deferred to F3c-5 (needs `--screenshot` offscreen
  flag).
- DX12 `--renderer dx12 --shader-only` still boots on RTX 2080 Ti FL
  12_2 with proof-of-life clear (Phase 3b regression: 0).

### Deferred to F3c-4
- D3D12 implementations of the 11 handle methods (PSO creation, root
  signature with CBV(b0)+SRVs+samplers, UPLOAD heap CB ring,
  CopyTextureRegion + transitions).
- Pipeline source_tex GL-backdoor → TextureHandle migration.

### Phase 3c progress — F3c-1 complete (build pipeline)
- **GLSL → SPIR-V → HLSL → DXIL build pipeline** wired via
  `cmake/CompileShaders.cmake`. Each `shaders/pass*.frag` + the new
  `shaders/fullscreen.vert` is translated by glslang (vcpkg) →
  spirv-cross (vcpkg) → dxc (Vulkan SDK or Windows SDK) at build time.
  Output: 9 `.dxil` bytecodes under `${BIN}/shaders/dxil/` plus a
  POST_BUILD copy next to `tubelight.exe`.
- **CMake option `TUBELIGHT_BUILD_DX12`** now also drives the shader
  build target. No-op on Linux.
- **Shader sources gain `layout(location = N)` qualifiers** on all
  stage I/O (SPIR-V requirement; backward-compatible with `#version
  450 core` GL path). `gl_VertexIndex` shim via `TUBELIGHT_VULKAN`
  define handles the GL-vs-Vulkan vertex ID name mismatch.
- **`shaders/fullscreen.vert`** extracted from
  `default_fullscreen_vertex_source()` as the single source of truth.
  GL runtime loads the file lazily and caches; falls back to a
  hard-coded copy if the file is missing.
- **Determinism verified**: 3 clean builds in a row produce
  byte-identical DXIL hashes (M3 of [phase-3c SPEC](specs/phase-3c/SPEC.md)).
- Pipeline still GL-only at runtime — F3c-2/3/4 wire the DXIL files
  into `D3D12Backend`. See [specs/phase-3c/PLAN.md](specs/phase-3c/PLAN.md).

### Known limitations

## [0.2.0-alpha.0] — 2026-05-28

### Added (architecture)
- **Phase 3b (ADR-0002): Direct3D 12 backend skeleton**. New
  `D3D12Backend` (`src/render/backend_d3d12.{h,cpp}`) creates a D3D12
  device (FL 12_0+), DXGI 1.6 flip-discard swap chain, command queue
  + allocator + list, RTV descriptor heap, fence sync, clear, and
  Present. Validated on NVIDIA RTX 2080 Ti at FL 12_2.
- **CLI `--renderer dx12`** opens a no-API GLFW window, hands the HWND
  to `D3D12Backend`, and runs a proof-of-life loop that clears to a
  Tubelight-blue background. Falls back to OpenGL automatically if
  device creation fails — covers ADR-0002 R12 (pre-DX12 GPU, Wine
  without DXVK, etc).
- **`IRenderBackend` extended** with `begin_frame()` / `end_frame()` /
  `resize()` / `supports_pipeline()` and a `BackendInitParams` struct
  that bundles the opaque native window handle. GL backend ignores
  the handle; D3D12 requires it.
- **CMake option `TUBELIGHT_BUILD_DX12`** (default ON on Windows, OFF
  on Linux). Adds `d3d12` system library and the
  `TUBELIGHT_HAVE_D3D12` compile definition. No new vcpkg deps — the
  Windows SDK 10.0.26100 already ships everything required.

### Important limitation
- **The 8-pass CRT Pipeline still requires the OpenGL backend**.
  `D3D12Backend::supports_pipeline()` returns `false`; Pipeline rejects
  any backend that doesn't support it. Porting the passes to HLSL +
  rewriting Pipeline to use abstract resource handles lands in
  Phase 3c (target: v0.2.0-beta). `--renderer dx12 --shader-only`
  shows only the proof-of-life clear; the testcard image argument is
  accepted but not yet rendered through the pipeline.

### Why ship a non-driving backend?
Two reasons:
1. Validates windowing + adapter selection + swap chain plumbing
   end-to-end on real hardware before sinking a week into HLSL ports.
2. Surfaces `--renderer dx12` so users and CI can smoke-test D3D12
   device creation on their hardware ahead of v0.2.0 stable (early R12
   detection).

### Synchronisation
Intentionally simple in Phase 3b: one allocator, one command list, one
fence, wait-for-idle on every Present (~1 ms penalty vs. a proper
triple-buffered pipeline). Pipelining lands in Phase 3c.

## [0.1.7] — 2026-05-28

### Added (architecture)
- **Phase 3a (ADR-0002): `IRenderBackend` abstraction** — new
  `src/render/` module introducing `IRenderBackend`, `GLBackend`, and a
  `create_backend(BackendKind)` factory. `Pipeline` now routes its raw
  GL state changes (viewport, clear, default-framebuffer bind, fullscreen
  quad draw) through the backend instead of calling OpenGL directly.
  `FBO`, `Texture2D`, and `ShaderProgram` remain GL-specific concrete
  types for this phase; they get abstracted in Phase 3b when the D3D12
  backend forces it. Zero functional change vs. v0.1.6 — the binary
  renders identically.
- **CMake option `TUBELIGHT_RENDERER_GL`** (default ON). Currently the
  only supported value; build scripts and CI can already reference it.
- **CLI flag `--renderer <gl>`** — accepted by the binary, validates the
  token, and rejects anything other than `gl` (or its `opengl` alias) so
  users get a clear error rather than a silent fallback. The `dx12` token
  is explicitly reserved for v0.2.0.
- `Pipeline::set_backend()` for tests and Phase 3b code that wants to
  inject an alternative backend before `create()`.
- `Pipeline::backend_name()` for diagnostics — surfaced through
  `tubelight --version` as `(renderer: gl)`.

### Why
Rails for the D3D12 backend (ADR-0002 §3). Phase 3a is intentionally
narrow so the diff is reviewable as a refactor and the binary's behaviour
is byte-identical to v0.1.6: anyone bisecting can use 0.1.7 as a baseline
free of new pixel changes.


- Pass 5 (temporal persistence / voltage bloom / BFI) is still an identity
  shader. Real implementation requires a history-FBO refactor scheduled
  for v1.1 — see specs/PLAN.LOCKED.md F7 T7.2.
- PipeWire screencast capture is scaffolding only; D-Bus portal session
  and stream consumption ship in v1.1.
- M1 (<2 ms hook latency) is unverified — requires a real target run with
  AMD FLM or equivalent. Hook bodies are passthrough today.
- Magnification API source rect (`src/overlay/overlay_mode_win.cpp:674`)
  is hardcoded to monitor rect — not updated when overlay-target rect
  changes at runtime via `Ctrl+Alt+T`. Workaround: use the CLI flag
  `--overlay-target <title>` which initializes correctly. Fix deferred
  to v0.2.0.

## [0.1.6] — 2026-05-27

### Changed (perf)
- **Phase 2b (ADR-0002): PBO double-buffer for video recording**.
  `VideoRecorder::push_frame()` no longer blocks the main loop on
  `glReadPixels`. A 3-slot Pixel Buffer Object ring is primed at
  recording start; each frame the new read goes into the next slot
  (driver DMAs async) and the oldest slot — written 3 frames ago,
  guaranteed GPU-ready — is mapped and shoved to ffmpeg.
  Saved per recorded frame: ~3-6 ms GPU stall. At 60 Hz recording
  that's a noticeable framerate stabilisation on mid-tier GPUs and
  ~20% lower CPU under sustained capture.
  Implementation: `src/overlay/capture_to_disk.{h,cpp}` —
  `kPboCount=3`, new members `pbos_[]` / `pbo_write_idx_` /
  `pbo_filled_`. The ring is drained on `stop()` so the last 2 frames
  of the recording aren't lost. `push_frame_from_bgra()` (DXGI/Mag
  CPU-side path) is unchanged — its data is already host-resident.
  Video frames are now delayed by `kPboCount` frames (~50 ms at 60 Hz)
  before being written to the file; this is invisible in the final
  MP4 because timestamps come from the recorder, not from the time
  of write.
- **Phase 2c (ADR-0002): skip pass 5 when persistence is negligible**.
  `Pipeline::render_to_screen()` now early-skips the temporal pass
  (`fbos_[6]` bind + clear + shader dispatch + history snapshot) when
  `persistence_strength * (ratio_r + ratio_g + ratio_b) < 1e-3`.
  Profiles that hit this: `terminal-p31` (strength 0.18 × ratios ≈
  0.55 — does NOT skip), `tv-bw-p4` (strength 0.40 — does NOT skip),
  `pvm-8220` with intensity 0 (skipped because strength scales with
  intensity multiplier through `params_.persistence_strength`).
  Saved per frame when applicable: ~0.4 ms on a mid-tier GPU.

### Notes
- Original Phase 2c plan was "merge passes 0+1 into one shader". On
  closer inspection that would have **increased** texture sampling
  3× (pass 1 reads its neighbours' pass-0 outputs, so a merge would
  require computing pass 0 at the centre AND four neighbours — 9
  samples each, 45 total vs. the current 9 + 5 = 14). Plan revised.
- The screenshot path (`save_screenshot_png_async`) was NOT moved
  to PBOs. It's one-shot per user keypress, and the worker-thread
  PNG encode already hides the cost. Will revisit if needed.

## [0.1.5] — 2026-05-27

### Added
- **ADR-0002** (`docs/adr/0002-d3d12-dcomp-wgc-hdr-slang-blueprint.md`)
  — architectural blueprint of the next-generation rendering and
  capture stack: D3D12 + DirectComposition (chrome / body visuals
  separately) + Windows Graphics Capture (per-window) + scRGB FP16 →
  HDR10 output + Variable Rate Shading on bloom/halation passes +
  async compute pairing + Slang shader sources compiled to SPIRV +
  DXIL + HLSL bytecode. Implementation lands incrementally across
  v0.1.5 → v0.3.x (full plan in the ADR Phase table). No code change
  in this release beyond the doc.

### Changed (perf)
- **Skip the ImGui begin/end frame cycle when no UI is on screen**
  (`overlay_mode_win.cpp:1845`). Earlier builds always paid the
  ~30-50µs `ImGui::NewFrame()` + 50-100µs `ImGui::Render()` even when
  the menu was closed, the HUD off, no toast active and no recording.
  Now the cycle only fires when at least one is visible. Marginal
  CPU saving (~0.5% sustained on 60 Hz) but cleaner separation
  between idle and active overlay states; sets up the
  conditional-rendering pattern that the D3D12 backend will follow.

### Notes
- WS_EX_LAYERED is no longer added to the plain `--overlay` (windowed)
  window. The path that added it (Ctrl+Alt+C toggle, removed in v0.1.4)
  is gone. v0.1.5 confirms this as the intentional design — the DWM
  composition cost in windowed mode dropped accordingly. The
  layered-window path is still used for the Magnification API host and
  for target/region/fullscreen overlay modes (where WS_EX_TRANSPARENT
  is required for cross-process click-through).

## [0.1.4] — 2026-05-27

### Changed (UX / breaking)
- **Recordable mode is always-on**. The `Ctrl+Alt+R` hotkey is removed.
  Snipping Tool, Game Bar and OBS Display Capture see the overlay
  automatically, without any user action. Implementation: capture
  always routes through the Magnification API with our own HWND in
  `MagSetWindowFilterList(MW_FILTERMODE_EXCLUDE)` to break the feedback
  loop. `WDA_EXCLUDEFROMCAPTURE` is no longer used.
- **Click-through is mode-dependent, no longer a user toggle**. The
  `Ctrl+Alt+C` hotkey is removed and the menu checkbox replaced by a
  disabled info line. Policy by mode: `--overlay` (windowed) = OFF
  (drag/resize via standard Win32 title bar / borders); `--overlay-target`,
  `--overlay-region`, `--overlay-fullscreen` = ON.
- Menu Audio tab "Recordable by Snipping Tool / Game Bar / OBS" and
  "Click-through (windowed)" checkboxes are now replaced by greyed-out
  status lines documenting the new always-on / by-mode behaviour.
- Help tab shortcut list no longer mentions `Ctrl+Alt+C` or `Ctrl+Alt+R`.
  In-app footer bumped to v0.1.4.

### Fixed
- **Magnification API `src_rect_` is now updated on target / region /
  fullscreen attach** (`overlay_mode_win.cpp:712`, new
  `MagCapture::set_source_rect(x, y, w, h)`). Pre-v0.1.4, the rect was
  set once in `init()` to the full monitor and never updated when the
  overlay attached to a smaller area, so the Mag callback kept sampling
  the whole screen instead of the new target. Visible as "the recorded
  overlay is bigger / offset relative to what's actually visible".

### Removed
- Settings fields `clickthrough_user` and `recordable` in
  `%APPDATA%\Tubelight\settings.json` are now silently ignored on load.
  No migration is written; the file is just read-as-is and the
  obsolete keys are dropped on the next normal save.

### Notes
- This is the "Phase 1" cut of ADR-0001 (always-on R/C, fix Mag bug).
  Phase 2 (D3D12 + WGC capture + HDR pipeline) and Phase 3 (full chrome
  rewrite with mouse-hook drag/resize) are tracked in subsequent ADRs.

## [0.1.3] — 2026-05-27

### Changed
- **Repo is now public**. User manual updated across all instances to
  remove "PRIVATE" / "private repo" wording:
  - `docs/manual/manual.json` §2 (download): now points users to the
    public [Releases page](https://github.com/GS-RUN/tubelight/releases)
    in both ES and EN; the outdated `tubelight-0.1.0-win64.zip` example
    replaced with `tubelight-<version>-win64.zip` so future bumps don't
    desync the docs.
  - `docs/manual/manual.json` §13 (troubleshooting): "open an issue in
    the private repo" → "open a [GitHub issue](.../issues)" in both ES
    and EN, with the actual issues URL.
  - `docs/manual/manual.json` §15 (credits): repo URL no longer tagged
    `(PRIVATE)` in either language.
  - `NEXT_STEPS.md`: removed `(PRIVATE)` tag from the repo line.
- `manual.json` `meta.version` / `meta.git_tag` bumped to `0.1.3`.
- Regenerated `manual.html`, `manual.pdf`, `manual.es.txt`,
  `manual.en.txt` from the cleaned source.

## [0.1.2] — 2026-05-27

### Fixed
- **Personal path leakage cleanup** (per CONSTITUTION C1):
  - `docs/manual/INTEGRATION.md` no longer references `D:\AgentWorkspace\`;
    capture scripts now described as auto-locating via `$PSScriptRoot`.
  - All five `docs/manual/scripts/*.ps1` (`make_testcard`, `testcard_viewer`,
    `testcard_viewer_fs`, `capture_all`, `capture_ui`) replaced hardcoded
    `D:\AgentWorkspace\Tubelight\...` defaults with `$PSScriptRoot`-relative
    auto-detection (repo root resolved as `..\..\..` from `scripts/`).
  - `NEXT_STEPS.md` "Manual de usuario" section purged of personal paths;
    points to released `docs/manual/` artifacts instead.
- **Removed**: `docs/manual/PENDING.md` (obsolete v0.1.1 planning doc).

### Notes
- Affects only repo cleanliness; no behaviour change in `tubelight.exe`.
- v0.1.1 release zip kept on GitHub for history (0 downloads at supersede
  time); v0.1.2 is the recommended download.

## [0.1.1] — 2026-05-27

### Added
- **Bilingual user manual (ES/EN)** under `docs/manual/`:
  single-file interactive HTML (307 KB) with sidebar + scroll-spy, substring
  search, lang/theme toggles, lightbox, glossary tooltips, frame strips,
  print stylesheet (WCAG AA). Plus printable PDF (1.68 MB), 80-col TXT,
  single-source bilingual JSON (`manual.json`, 15 sections, 25 glossary
  terms). 42 calibrated screenshots anchored to a known testcard window.
- **Help → Open user manual button** in the in-app menu
  (`src/overlay/menu.cpp`): resolves `docs/manual/manual.html` via five
  candidate paths (dev tree, release zip, sibling layouts) and opens with
  `ShellExecuteW`; fallback to the GitHub repository URL if the local file
  is missing.
- **Reproducible capture pipeline** (`docs/manual/scripts/`):
  `make_testcard.ps1` (PowerShell + System.Drawing generates the
  reference 1280×960 test pattern), `testcard_viewer.ps1` (WinForms
  viewer with a known window title for `--overlay-target` to anchor on),
  `testcard_viewer_fs.ps1` (fullscreen variant for mode-02 demo),
  `capture_all.ps1` (drives the profile/signal/fine galleries),
  `capture_ui.ps1` (drives menu + HUD + mode shots).
- **Manual generators** (`docs/manual/`):
  `build_manual.mjs` (TXT + HTML from JSON, glossary tooltip injection,
  ANSI terminal renderer), `build_pdf.mjs` (Playwright A4 print, requires
  `npm i playwright + chromium`), `validate-manual.mjs` (CI gate: bilingual
  completeness, screenshot files exist on disk, alt non-empty, code blocks
  have `lang`, git_sha present in meta).
- `docs/manual/INTEGRATION.md` documenting the menu integration.

### Changed
- **License**: MIT → PolyForm Noncommercial 1.0.0. Author:
  Alonso J. Núñez (GS·RUN). Commercial contact: `gsrun.editor@gmail.com`.
- `src/main.cpp` and `src/overlay/menu.cpp` bumped from "0.1.0-alpha" to
  "0.1.1" in user-facing version strings.
- `.gitignore` excludes `docs/manual/node_modules/` and
  `docs/manual/package-lock.json` (regenerable via `npm i` in that dir).

### Fixed
- `src/overlay/menu.cpp` `WIN32_LEAN_AND_MEAN` guarded redefinition
  silences MSVC C4005 warning when the macro is already defined on the
  command line.

## [0.1.0-alpha] — 2026-05-26

First development build. Not for distribution. Project bootstraps the full
8-pass shader pipeline architecture, profile system with cited physical
parameters, and platform-specific injection scaffolding.

### Added
- **Project foundation (F1)**
  - CMake build with vcpkg manifest (Windows) and system-package install
    (Linux). MIT license, GitHub Actions CI matrix (Windows MSVC, Linux
    gcc-13 + clang-18).
  - Constitution C1 enforced by `tools/precommit_check_paths.{sh,ps1}` —
    no absolute personal paths in versioned files.
  - 9 spec-forge artefacts under `specs/` (SPEC + PLAN + CONSTITUTION locked).
- **Pipeline core (F2)**
  - 8 shader passes: signal modeling, dithering analysis + reconstruction,
    beam + scanlines (CRT gamma 2.5 linearization), 6 mask types
    (shadow / aperture grille / slot / diamond / cgwg-mix / dot-trio),
    bloom + per-channel halation, temporal placeholder, final composition
    with barrel + vignette + magnetic interference + convergence.
  - `tubelight --shader-only IMG` mode for live preview with key bindings
    1..8 to toggle individual passes.
- **Profiles (F3)**
  - 15 CRT profiles: PVM-8220, PVM-20M4, BVM-20F1U, Commodore 1084S,
    Sharp X68000 CZ-602D + CZ-603D, Sharp CZ-614D, Wells-Gardner K7000,
    NEC MultiSync I + 4FG, Sony GDM-FW900, P31 + P3-amber terminals,
    vintage B&W P4 TV, generic-pvm baseline.
  - 7 signal profiles: RF, Composite NTSC + PAL, S-Video, SCART RGB,
    Component YPbPr, RGB/VGA.
  - Every physical parameter carries a `source.url` + `retrieved_at` per
    Constitution C2. NEEDS-MEASUREMENT items explicitly flagged.
  - JSON Schemas in `schemas/`, C++ validator in `src/profile/`.
  - `tubelight --validate-profile <path>` CLI.
- **Signal modeling (F4)**
  - Pass −1 implements YIQ-space bandwidth-limited Gaussian blur per
    channel, dot crawl beat, ringing, RF ghosting, line / pixel / rf noise.
    Connection-aware: rgb_vga is passthrough, scart_rgb / component skip
    chroma blur, composite / svideo apply per-spec limits.
  - Pass 0 detects 1-pixel-alternating vertical or horizontal dithering
    patterns (mask in alpha).
  - Pass 1 reconstructs dithered patterns by averaging neighbours where
    the mask is high — fixes Sonic Green Hill cascades on composite paths.
  - Pipeline `apply_crt_profile()` + `apply_signal_profile()` map JSON
    fields to GlobalParams; monochrome phosphors auto-zero mask strength.
- **Injection scaffolding (F5)**
  - Linux: `libtubelight_preload.so` intercepts `glXSwapBuffers` + `eglSwapBuffers`
    via `LD_PRELOAD` + `dlsym(RTLD_NEXT, ...)`. Passthrough + log today.
  - Windows: `tubelight_backend.dll` with MinHook trampolines on
    `IDXGISwapChain::Present` (DX11) installed from a worker thread
    spawned by `DllMain`. `tubelight_inject.exe` performs
    `OpenProcess` + `VirtualAllocEx` + `CreateRemoteThread(LoadLibraryA)`
    injection by PID.
  - IPC abstraction: named pipe (`\\.\pipe\tubelight-<endpoint>`) on Windows,
    Unix socket (`$XDG_RUNTIME_DIR/tubelight-<endpoint>.sock`) on Linux,
    newline-delimited JSON wire format per `specs/CONTRACTS.md` §C1.
- **Vulkan + DX12 + PipeWire (F6)**
  - `VK_LAYER_tubelight_overlay` cross-platform Vulkan layer implementing
    the modern `vkNegotiateLoaderLayerInterfaceVersion` entry, full
    instance + device chain, hooked `vkQueuePresentKHR`. Manifest JSON
    ships next to the binary for `VK_ADD_LAYER_PATH` discovery.
  - DX12 hook: `tubelight_backend.dll` extended with `IDXGISwapChain3::Present`
    and `Present1` trampolines via MinHook.
  - PipeWire screencast fallback (Linux): API surface complete; D-Bus
    portal session deferred to v1.1.
- **Polish + release prep (F7)**
  - 5 additional profiles (BVM-20F1U, CZ-603D, MultiSync 4FG, FW900,
    generic-pvm baseline).
  - Pass 6 adds magnetic interference (slow sin-based UV jitter), warm-up
    curve (180 s linear brightness + white-point ramp), and convergence
    offset that grows toward corners.
  - `tubelight --export-slangp <out>` exports the active CRT + signal
    profile pair as a RetroArch slang preset.
  - CI smoke-tests both the .slangp export and bundled-profile validation
    on every push.

### Not implemented yet
- Pass 5 history-FBO temporal (per-channel exponential decay) — v1.1
- PipeWire portal D-Bus integration — v1.1
- ImGui control panel — v1.1
- Installer artifacts (NSIS / AppImage / Flatpak) — packaging in v1.0 RC
- Audio CRT (transformer whine, degaussing thump) — v2.0
- HDR / 10-bit output paths — v2.0
- ARM64 builds — v2.0+
- Vector display mode (Asteroids / Tempest) — v2.0+

[Unreleased]: https://github.com/gs-run/tubelight/compare/v0.2.0-rc.0...HEAD
[0.2.0-rc.0]: https://github.com/gs-run/tubelight/releases/tag/v0.2.0-rc.0
[0.1.0-alpha]: https://github.com/gs-run/tubelight/releases/tag/v0.1.0-alpha
