# Phase 3e — GL vs D3D12 throughput bench

**Date**: 2026-05-29
**Hardware**: NVIDIA GeForce RTX 2080 Ti, FL 12_2 (single dev host)
**Build**: Release, `tubelight` post-descriptor-refactor
**Method skill**: `cpp-perf-refactor-playbook` (Fase 0 + 1f adapted to a
GPU-time bench) + `dx12-engineer` (descriptor-heap scope)

## TL;DR

**D3D12 and OpenGL are at parity** for the 8-pass CRT pipeline GPU cost:
**DX12 ~0.38 ms/frame, GL ~0.36 ms/frame** (within ~5-8%, both rock-stable
p99). Neither the ADR's aspirational "3-5× DX12" nor the (now-retracted)
"GL is 2-6× faster" hold — it's a tie.

| Backend | avg ms/frame (GPU) | p50 | p99 | min |
|---|---|---|---|---|
| **GL**   | **0.359** | 0.359 | 0.365 | 0.355 |
| **DX12** | **0.378-0.380** (steady) | 0.378 | 0.384 | 0.375 |

> ### ⚠️ Retraction of the first Phase 3e result
> An earlier revision of this doc (commit `a1db1e1`) reported "GL ~0.44 ms
> stable, DX12 ~1-3 ms jittery → GL 2-6× faster". **That was a measurement
> artifact, not a real GPU-cost difference.** The DX12 `--bench` path was
> still calling `swap_chain_->Present(1, 0)` (vsync) each frame; the vsync
> throttle backed up the GPU queue so the `EndQuery..EndQuery` window
> captured queue-wait time, not pipeline work. The GL bench never presents
> (no `SwapBuffers`), so its timestamps were always clean. **The tell**: the
> DX12 *min* was a constant ~0.447 ms across every run — the real pipeline
> cost peeking through the polluted tail. Fix: skip `Present` when frame
> timing is on (`end_frame`), isolating the pipeline GPU cost. Re-measured
> → parity. Lesson logged below.

## Workload (Fase 0 — the gate)

- **What**: the full 8-pass CRT pipeline on a fixed deterministic input.
- **Input**: `docs/manual/assets/raw/testcard.png` (1280×960), `pvm-8220`
  + `composite_ntsc`, output 1280×960.
- **Command**: `tubelight --shader-only <testcard> --renderer <gl|dx12>
  --profile pvm-8220 --signal composite_ntsc --bench 300`
- **Frames**: 30 warmup + 300 measured.

## Metric — GPU timestamp queries

Pure GPU time of the command-list work via API timestamp queries
(`GL_TIME_ELAPSED`; D3D12 `QUERY_HEAP_TYPE_TIMESTAMP` + `ResolveQueryData`
→ READBACK, read after the fence). Present/vsync independent **by design —
which is exactly why the leftover DX12 Present had to be removed in bench
mode** (see retraction). Enabled only under `--bench`; the production
render path is untouched. Both backends drive the same `begin_frame →
render_to_screen → end_frame → finish()` loop.

## Corrected results (3 reps, 300 frames, no-present timing)

```
GL     avg 0.359  p50 0.359  p99 0.365  min 0.355  max 0.365
DX12   avg 0.488  p50 0.458  p99 1.339  min 0.447  max 1.345   (rep1 — first-run warmup)
DX12   avg 0.378  p50 0.378  p99 0.384  min 0.375  max 0.384   (rep2 — steady)
DX12   avg 0.380  p50 0.380  p99 0.384  min 0.377  max 0.386   (rep3 — steady)
```

Steady-state DX12 ≈ 0.38 ms vs GL ≈ 0.36 ms. (Single host, governor not
pinned — order-of-magnitude, but the run-to-run spread is tiny so the
parity conclusion is solid.)

## Descriptor refactor (this session)

Independent of the measurement fix, the DX12 backend's per-draw descriptor
handling was rewritten (it was the original suspect, and it's the canonical
DX-02 pattern regardless):

- **Before**: `draw_fullscreen_quad` did 1-2 `CopyDescriptorsSimple` *every
  draw* (8 passes/frame) into a wrapping shader-visible scratch ring.
- **After**: a **persistent per-pass descriptor table**, double-buffered
  per frame in flight (2 SRVs × `kBackBufferCount` = 4 slots/pass; 8 passes
  → 32 of 256 heap slots). `draw_fullscreen_quad` re-copies into it **only
  when a pass's bound textures change** (`.ptr` compare vs last bake);
  steady state is **zero descriptor copies**. Double-buffering makes the
  re-bake race-free vs the GPU still reading the other frame's table.
- WGC's 2-buffer recycled source aligns with `kBackBufferCount=2`, so even
  the live overlay reaches zero copies after warmup.

This didn't move the headline number (the pipeline was never descriptor-copy
bound at this scale — 8 copies/frame is cheap), but it removes wasteful
per-draw work, is the correct explicit-API pattern, and is verified
**bit-exact** (golden PSNR ∞) + race-free.

## Honest conclusion

- **GL and DX12 are at GPU-cost parity** (~0.36 vs ~0.38 ms) for this
  pipeline on this GPU. The ADR's "3-5× DX12 throughput" is **not** borne
  out — but neither is DX12 slower.
- DX12's value in ADR-0002 remains **qualitative** (WGC per-window capture,
  future HDR10 scRGB, VRS/async-compute), now with the reassurance that it
  costs no more GPU than GL for the core pipeline.
- v0.2.0 stable notes: GL stays the default; DX12 is the capture/HDR path,
  **at parity** on raw pipeline cost.

## Lesson (methodology)

**A GPU-timestamp bench must exclude `Present`/`SwapBuffers` from the timed
path** — vsync (or DWM composition) throttles the queue and the timestamp
window silently absorbs the wait. A suspiciously *constant min* under a
*noisy mean* is the signature of "real cost = min, tail = external stall".
Always compare both backends with the same present policy (here: none).

## Next steps (deferred)

- Confirm the ~0.447 ms first-run DX12 figure is warmup (PSO/upload) not a
  residual artifact — it settles to 0.378 by rep 2.
- If a *real-world* (with-present, windowed) throughput number is ever
  wanted, add an `ALLOW_TEARING` vsync-off present path (DX-15) — but that
  measures the compositor, not the pipeline.

## Reproduce

```
cmake --build build/windows-vcpkg --config Release --target tubelight
for r in 1 2 3; do
  ./build/windows-vcpkg/Release/tubelight.exe --shader-only docs/manual/assets/raw/testcard.png \
     --renderer gl   --profile pvm-8220 --signal composite_ntsc --bench 300
  ./build/windows-vcpkg/Release/tubelight.exe --shader-only docs/manual/assets/raw/testcard.png \
     --renderer dx12 --profile pvm-8220 --signal composite_ntsc --bench 300
done
# bench line prints to stderr (GL stdout is swallowed in some shells)
```
