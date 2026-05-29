# Phase 3e — GL vs D3D12 throughput bench

**Date**: 2026-05-29
**Hardware**: NVIDIA GeForce RTX 2080 Ti, FL 12_2 (single dev host)
**Build**: Release, `tubelight` post-`00c0ebb` (DX12 N-frame-in-flight sync)
**Method skill**: `cpp-perf-refactor-playbook` (mode=audit, Fase 0 + 1f
adapted to a GPU-time bench)

## TL;DR

**The ADR-0002 "3-5× DX12 throughput" estimate is refuted in the current
implementation.** Measured the opposite: **OpenGL is ~2-6× faster** than
the young D3D12 backend for the 8-pass CRT pipeline.

| Backend | avg ms/frame (GPU) | fps | p50 | p99 | run-to-run |
|---|---|---|---|---|---|
| **GL**   | **~0.44** | ~2200-2400 | ~0.43 | ≤1.2 | stable (0.41-0.47) |
| **DX12** | **~1-3**  | ~300-1000  | 0.86-5.1 | 7-9 | jittery (0.99-3.19) |

GL is consistent; DX12 is both slower on average **and** much jitterier
(p99 7-9 ms, max up to 9.25 ms).

## Workload (Fase 0 — the gate)

- **What**: the full 8-pass CRT pipeline applied to a fixed deterministic
  input, the only thing worth comparing across backends.
- **Input**: `docs/manual/assets/raw/testcard.png` (1280×960), CRT profile
  `pvm-8220`, signal `composite_ntsc`. Window/output 1280×960.
- **Command**:
  ```
  tubelight --shader-only docs/manual/assets/raw/testcard.png \
            --renderer <gl|dx12> --profile pvm-8220 --signal composite_ntsc \
            --bench 300
  ```
- **Frames**: 30 warmup (PSO/shader/driver settle) + 300 measured.

## Metric — GPU timestamp queries (present/vsync independent)

Wall-clock-with-vsync-off was rejected: in windowed flip-model, DWM caps
presentation regardless, and forcing `ALLOW_TEARING` would change the
production present path of the just-shipped v0.2.0-rc.0. Instead the bench
measures **pure GPU time of the command-list work** via API timestamp
queries, which is present- and vsync-independent and additive (zero risk
to the render path):

- **GL**: `GL_TIME_ELAPSED` query wrapping `begin_frame`..`end_frame`
  (i.e. the 8 passes). `glFinish()` + `glGetQueryObjectui64v`.
- **DX12**: 2-slot `D3D12_QUERY_HEAP_TYPE_TIMESTAMP`; `EndQuery` at frame
  start + end, `ResolveQueryData` to a READBACK buffer, read after the
  fence via `GetTimestampFrequency`.

Enabled only under `--bench` (`set_frame_timing`); normal rendering is
untouched. Both backends drive the **same** `begin_frame → render_to_screen
→ end_frame → finish()` loop.

## Raw results (3 reps each, 300 frames)

```
GL     rep1 avg 0.411  p50 0.430  p99 0.447  min 0.363  max 1.496
GL     rep2 avg 0.453  p50 0.434  p99 1.182  min 0.359  max 1.188
GL     rep3 avg 0.468  p50 0.440  p99 1.186  min 0.429  max 1.413
DX12   r0   avg 0.992  p50 0.859  p99 4.466  min 0.449  max 4.651
DX12   rep1 avg 3.193  p50 5.140  p99 7.933  min 0.448  max 7.950
DX12   rep2 avg 2.230  p50 2.411  p99 7.039  min 0.449  max 9.043
DX12   rep3 avg 2.605  p50 1.133  p99 9.036  min 0.449  max 9.255
```

(ms per frame, GPU time. Single host, governor not pinned — treat as
order-of-magnitude, not lab-grade. Variance is reported honestly rather
than averaged away.)

## Diagnosis (hypotheses, not yet confirmed)

The DX12 backend is young (Phase 3b skeleton → 3c pipeline → 3d overlay →
3e sync) and has **not** been perf-tuned. Prime suspects for the slowdown
+ jitter, in likely-impact order:

1. **Per-draw `CopyDescriptorsSimple`** — every `draw_fullscreen_quad`
   copies a 2-SRV descriptor table from the CPU staging heap into the
   shader-visible scratch ring. 8 passes/frame × a copy each. GL binds
   textures/UBOs directly with no per-draw descriptor shuffling.
2. **Two PSOs per pass** (intermediate RGBA16F + backbuffer RGBA8) +
   `SetPipelineState` churn per pass.
3. **CB ring `SetGraphicsRootConstantBufferView`** + barrier transitions
   per pass.
4. **Timestamp resolve** could itself add a small per-frame stall (the
   constant ~0.448 ms DX12 *min* is suspicious — possibly a measurement
   floor rather than real GPU work).

None of these are bugs — they are the cost of an un-tuned explicit-API
backend. The fix is descriptor/PSO management work, **not** in scope here.

## Honest conclusion

- For raw 8-pass throughput **today, GL wins decisively** on this GPU.
- DX12's value in ADR-0002 is **qualitative** (WGC per-window capture,
  future HDR10 scRGB, VRS/async-compute) — not brute throughput yet.
- **Do not ship a "DX12 is faster" claim.** v0.2.0 stable notes should
  state GL remains the performance default; DX12 is the path for the
  capture/HDR features.

## Next steps (deferred, own session)

- **P0** — DX12 descriptor refactor: pre-bake per-pass descriptor tables
  once instead of `CopyDescriptorsSimple` per draw; re-bench. Hypothesis:
  closes most of the gap. (`dx12-engineer` descriptor scope +
  `cpp-perf-refactor-playbook` execute.)
- **P1** — confirm the ~0.448 ms DX12 floor is/ isn't a timestamp artifact
  (compare against a CPU wall-clock vsync-off run with ALLOW_TEARING in a
  throwaway branch).
- Re-run this bench after each DX12 perf change; keep the table updated.

## Reproduce

```
cmake --build build/windows-vcpkg --config Release --target tubelight
for r in 1 2 3; do
  ./build/windows-vcpkg/Release/tubelight.exe --shader-only docs/manual/assets/raw/testcard.png \
     --renderer gl   --profile pvm-8220 --signal composite_ntsc --bench 300
  ./build/windows-vcpkg/Release/tubelight.exe --shader-only docs/manual/assets/raw/testcard.png \
     --renderer dx12 --profile pvm-8220 --signal composite_ntsc --bench 300
done
# bench line is printed to stderr (GL stdout is swallowed in some shells)
```
