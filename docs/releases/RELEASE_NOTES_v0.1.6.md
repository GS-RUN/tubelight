# Tubelight v0.1.6

**Date**: 2026-05-27
**Author**: Alonso J. Núñez (GS·RUN)
**License**: PolyForm Noncommercial 1.0.0 — commercial: `gsrun.editor@gmail.com`
**Supersedes**: v0.1.5

## Highlights

ADR-0002 Phase 2b + Phase 2c — surgical OpenGL perf wins on the
recording and temporal-pass paths. No API change. No visual change.
~3-6 ms GPU stall removed per recorded frame, ~0.4 ms saved per
clean-signal frame.

## Changes

### Changed (perf)
- **Phase 2b: PBO double-buffer for video recording**.
  `VideoRecorder::push_frame()` was the largest single GPU stall in
  the running overlay — `glReadPixels` to host memory is synchronous
  by default and at 1920×1200 takes 3-6 ms with the driver waiting
  for the GPU to finish the current frame first. A 3-slot Pixel
  Buffer Object ring now buffers the readback: the new frame's
  `glReadPixels` writes asynchronously into PBO[N % 3], and the slot
  that was written 3 frames ago — guaranteed GPU-ready — is mapped
  and sent to ffmpeg. Recorded frames now lag the live overlay by
  ~50 ms (3 × 16.6 ms at 60 Hz), which is invisible in the saved MP4
  because the recorder timestamps don't change.
- **Phase 2c: skip pass 5 when persistence is negligible**.
  `Pipeline::render_to_screen()` early-skips the temporal pass FBO
  bind + clear + shader dispatch + history snapshot when
  `persistence_strength × (ratio_r + ratio_g + ratio_b) < 1e-3`.
  Most monochrome / scope-style profiles still need the pass; PVM
  profiles at low intensity skip cleanly. Saved: ~0.4 ms per frame
  when applicable.

### Notes
- Original Phase 2c plan ("merge passes 0+1 into one shader") was
  abandoned during implementation review — it would have **tripled**
  texture sampling (pass 1 reads pass 0 at its 4 neighbours, so a
  merge would require running pass 0's 9-sample neighbourhood at the
  centre AND four positions = 45 samples vs. current 9+5=14).
  Reverted to the simpler, real win (pass 5 skip).
- Screenshot path (`save_screenshot_png_async`) was left as
  synchronous `glReadPixels`. It's one-shot per user keypress and
  the worker-thread PNG encode already hides its cost; not worth
  the complexity in this release.

### Internal
- `src/overlay/capture_to_disk.h`: 3 new `VideoRecorder` members
  (`pbos_[]`, `pbo_write_idx_`, `pbo_filled_`) and `kPboCount` const.
- `src/overlay/capture_to_disk.cpp`: `start()` initialises the PBO
  ring with `GL_STREAM_READ` hint; `push_frame()` rewrites the
  readback path; `stop()` drains the remaining ring before closing
  ffmpeg pipe.
- `src/core/pipeline.cpp`: 8 lines added at the top of
  `render_to_screen()` to compute `skip_pass5` once per frame and
  the loop's enable-check now consults it.
- Version bumps: CMake 0.1.5 → 0.1.6; `kVersion` string; ImGui Help
  footer; manual.json meta version.
- Manual HTML/PDF/TXT regenerated.

## Distribution layout

Same as v0.1.5. `tubelight-0.1.6-win64.zip` ~55 MB.

## Next phases (per ADR-0002)

| Phase | Ships in |
|---|---|
| 3a — `IRenderBackend` abstraction + GL backend wrap | v0.1.8 |
| 3b — D3D12 backend skeleton | v0.2.0-alpha |
| 3c — D3D12 backend full pipeline | v0.2.0-beta |
| 3d — WGC capture native | v0.2.0-rc |
| 3e — bench + v0.2.0 stable | v0.2.0 |

v0.1.7 was originally planned for Phase 2c standalone; since 2b and
2c shipped together in v0.1.6, the numbering jumps forward — the
next release will be v0.1.7 with Phase 3a (renderer abstraction).
