# Audit 06: GPU, Media & Visual I/O

**Date:** 2026-06-26
**Status:** Audited 2026-06-05 (verify-gated; 10 candidates → 3 confirmed, 7 dismissed)

## Purpose

Audit GPU runtime services and visual media paths for resource lifetime, shader contract, platform interop, capture/readback, and media decode robustness risks.

## Scope

- `src/runtime/gpu/`
- `docs/runtime/gpu.md`
- GPU and media-related operators under `operators/gpu/`
- Movie decode/session/upload shared code under `operators/shared/`
- Webcam, Syphon, texture loader, movie, capture, and analysis paths
- GPU, media, operator, and integration tests

## Primary Questions

- [ ] Are WebGPU, Metal, texture, and buffer lifetimes explicit and leak-resistant?
- [ ] Are WGSL parsing and uniform layout rules consistent between runtime and operators?
- [ ] Do media decode/upload/session paths handle reload, seek, failure, and shutdown safely?
- [ ] Are visual I/O operators resilient to missing devices, missing files, and unsupported codecs?
- [ ] Are capture, screenshot, readback, and analysis paths synchronized correctly?
- [ ] Are GPU-backed lane or texture outputs documented and tested?
- [ ] Are platform-specific paths isolated enough for future cross-platform work?

## Subsystem Checklist

- [ ] Trace GPU context initialization, resize, frame submission, and shutdown.
- [ ] Review WGSL header parsing and uniform layout assumptions against representative GPU operators.
- [ ] Inspect movie decode worker/session/queue/upload lifetimes.
- [ ] Check webcam and Syphon behavior for unavailable devices and platform stubs.
- [ ] Review texture readback, screenshot, mipmap, and analysis utilities for synchronization hazards.
- [ ] Verify tests cover shader parse failures, missing media, reload during playback, and capture correctness.
- [ ] Identify repeated GPU resource setup patterns that should become helpers or API contract docs.

## Audit Checklist

- [ ] Read the relevant subsystem docs and navigation guides.
- [ ] Inspect the main source files and ownership boundaries.
- [ ] Review tests that claim to cover the subsystem.
- [ ] Check docs/code/test contract drift.
- [ ] Identify correctness, robustness, and maintainability findings.
- [ ] Record findings with severity, category, evidence, and recommendation.
- [ ] Propose immediate, near-term, and backlog follow-up work.

## Findings

This subsystem audited **clean on correctness and resource-lifetime**: the verify pass refuted 7 of 10
candidates, including **both** "High correctness" findings (uniform-layout validation and GPU-ownership
docs — see Dismissed) and four "missing docs/tests" claims that already exist. The 3 confirmed findings
are all **Low** (one media-robustness quality gap, two GPU test-gaps). **No resource-lifetime defects**:
WebGPU handle lifecycles (instance→adapter→device→queue→surface, released in reverse in
`GpuContext::shutdown()`) and the deferred per-node texture release (`kGpuReleaseGraceFrames=3`, owned by
`FrameExecutor`, force-drained on teardown) are correct and **already documented** in `gpu_context`/
`frame_executor.h` comments + `src/runtime/gpu/CLAUDE.md`.

### Media (owner: `movie_file` operator / decoder factory)

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| 06-F3 | Low | Robustness | The decoder factory computes a precise failure reason (`avf_open_failed`, `codec='<fourcc>'`, `notchlc_decoder_unavailable`, …) but `movie_file` **discards** it — only an empty path / sustained-nil-frames *heuristic* surfaces via `run_checks`, so a *set* path to a bad/unsupported file is detected late and without the known reason | `operators/gpu/movie_file/movie_file.cpp:1152,1212-1232`; `operators/shared/movie_decode/decoder_factory.cpp` |

### GPU (owner: `FrameExecutor` / readback + analysis)

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| 06-F5 | Low | Test gap | No test that a GPU **texture output** survives a recompile / hot-reload and stays correctly routed to downstream sinks | `tests/gpu/test_gpu_operators.cpp` |
| 06-F6 | Low | Test gap | The GPU **readback→analysis** N-1-frame synchronization invariant isn't directly tested (production `TextureReadback`/`GpuFrameAnalysis` are untouched by tests) | `src/operator_api/texture_readback.h:62-132`; `src/runtime/gpu/gpu_frame_analysis.h:26-79` |

> All three were filed **Medium** and **downgraded to Low** by the verify pass — 06-F3 because `run_checks`
> already surfaces empty-path + decode-stall guidance (the residual is the *discarded precise reason* +
> heuristic-only detection); 06-F5 because full-recompile + `classify_hot_reload` rejecting `has_process_gpu`
> changes structurally prevent the stale-pointer variant; 06-F6 because the blast radius is the 4×4
> lightweight analysis metrics (brightness/contrast/hue/hash), not core rendering.

### Evidence & Recommendation

**06-F3 — `movie_file` discards the decoder's precise failure reason** (Low, Robustness · media)
- *Evidence:* `decoder_factory.cpp` populates `DecoderLoadResult.diagnostics` with concrete reasons
  (`codec='<fourcc>'`, `avf_open_failed`, `hap_open_failed`, `notchlc_decoder_unavailable`,
  `video_unsupported_platform`). `movie_file.cpp:1152` copies it into `LoadResult.diagnostics`, but
  `apply_ready_load_result` (`:1212-1232`) on `!success` just resets the decoder and calls
  `show_placeholder()` — it **never reads `diagnostics`**. So the already-known root cause is dropped.
  `run_checks` (`control_server_checks.cpp`) does surface `movie_file_path_empty` and
  `movie_sustained_nil_frames` ("decode stalling — check codec compatibility"), but a *set* path to a
  missing/unsupported file only trips the nil-frame heuristic (>60 frames, >50% nil), not a load-time error.
- *Impact:* On a bad/unsupported movie path the user gets a black frame and, at best, a delayed generic
  "decode stalling" check — never the specific "unsupported codec X" / "open failed" reason the code
  computed. Low (not silent — partly covered — but lower-quality diagnostics than the code already has).
- *Recommendation:* On load failure, surface `LoadResult.diagnostics` — log it and/or expose it on the
  existing `metal_import_failures` analysis-output channel, and feed a load-time check into `run_checks`
  (e.g. `movie_load_failed` with the reason). Small, high-leverage (reuses an already-computed string).

**06-F5 — No GPU texture-output-across-recompile test** (Low, Test gap · GPU)
- *Evidence:* `test_gpu_operators.cpp` (760 lines) builds fresh graphs and analyzes pixels but never
  recompiles/reloads and re-checks texture routing. No test asserts a texture-output's downstream
  connection survives a topology change / hot-reload. *(Note: the finder mischaracterized
  `test_gpu_lane_promotion.cpp` as a stub — it's a complete 4-case test, but of lane→GPU **promotion**,
  not texture-output identity, so it doesn't close this gap.)*
- *Impact:* A routing regression would show only as visual corruption. Bounded: `CompiledGraph` is rebuilt
  from scratch on every topology change and `classify_hot_reload` rejects GPU-capability changes, so the
  in-place-reuse failure mode is structurally prevented.
- *Recommendation:* Add a test that builds a GPU-texture-output graph, recompiles (and/or simulates
  reload), and asserts the downstream sink still receives correct output. Mirror the audit-01 lane tests.

**06-F6 — GPU readback→analysis sync untested** (Low, Test gap · GPU)
- *Evidence:* `TextureReadback::readback()` double-buffers (writes buffer B while mapping A;
  `compute_metrics()` is documented "call before queue_readback — reads previous frame's data", and
  `frame_executor.cpp` calls them in that order). Grep: **no** test references `TextureReadback`,
  `GpuFrameAnalysis`, `compute_metrics`, or `queue_readback` — `test_gpu_correctness.cpp` uses its own
  *synchronous* readback helper, not the production double-buffered path.
- *Impact:* A timing refactor could analyze the wrong frame's data. Bounded to the lightweight analysis
  metrics (display/modulation), not rendered output; double-buffer guards (`buf_busy_`, `mapping_pending_`)
  prevent the worst corruption.
- *Recommendation:* Add a test that queues a known pattern, asserts analysis lags by exactly one frame, and
  checks skipped-readback safety.

### Test Gaps (device-free vs platform-dependent)

- GPU texture-output identity/routing across recompile + hot-reload (06-F5) — device-free-ish (needs GPU).
- GPU readback→analysis 1-frame-lag correctness + skipped-readback safety (06-F6).
- Media decode error paths surfacing the *precise* reason (missing/unsupported set-path at load time) (06-F3).
- Multi-output GPU operators (aux textures): allocation / lifetime / downstream use.
- GPU context resize-during-frame; device-lost callback recovery.
- Webcam/Syphon device-unavailable graceful degradation (platform-dependent; macOS).

### Docs to Update
- `src/runtime/gpu/gpu_context.h` — a class-level ownership summary would help (the facts are in
  `CLAUDE.md` + inline comments; basis of dismissed 06-F1).
- `docs/runtime/gpu.md` — a "Uniform Layout Contract" note for the *programmatic* (codegen) GPU-operator
  path (WgslFilterBase authors don't touch layout; basis of dismissed 06-F2/F4).
- `operators/CLAUDE.md` — a "Platform-Specific Operators" note: `WebcamIn`, `SyphonIn/Out`, `AVFDecoder`,
  `AVFAudioExtractor` are macOS-only.

### Platform-specific risk (cross-platform blocker, latent)

`WebcamIn` is **macOS-only in practice**: `cmake/operators.cmake:493-519` *does* build it on non-Apple
(linking only the codegen output), but `enumerate_cameras()` / `create_avf_capture()` are **only defined**
in `avf_capture.mm` (Apple-only), so a non-macOS build would hit an **undefined-symbol link error** (not
the header/compile error the dismissed 06-F8 claimed). Latent today (only macOS is built); a real
cross-platform blocker to fix before any Windows/Linux port — add a non-Apple stub or gate registration.

## Follow-up

**Immediate** — none. No correctness / resource-lifetime defect.

**Near-term** — ✅ **DONE 2026-06-05** (build + media tests green)
- 06-F3: `movie_file` now **logs** the decoder's computed failure reason on load failure
  (`[movie_file] load failed: <diagnostics>`) instead of silently discarding it behind the black
  placeholder. Fuller surfacing (a `run_checks` `movie_load_failed` finding / MCP diagnostic that reads
  the reason at load time) is left as backlog — it needs operator→checks plumbing.

**Backlog**
- 06-F3 (fuller): a load-time `run_checks` finding carrying the reason (vs the current nil-frame heuristic).
- 06-F5: GPU texture-output-across-recompile test (GPU-dependent harness).
- 06-F6: GPU readback→analysis sync test (GPU-dependent harness).
- WebcamIn non-Apple stub / registration gate (before any cross-platform port).
- Apply the doc clarifications above (gpu_context ownership summary, uniform-layout contract note,
  platform-operators note).

### Dismissed (verification-refuted)

Seven candidates were refuted — notably both "High" findings:

- **06-F1** (HIGH→none, GPU ownership "undocumented") — refuted: ownership + grace-period are documented in
  `src/runtime/gpu/CLAUDE.md` and 8-line inline comments; shutdown order correct; force-drain on teardown.
- **06-F2** (HIGH→none, "no uniform-layout validation") — refuted: the C++ UBO is **single-sourced from the
  WGSL** by codegen, which emits `static_assert(sizeof/alignof/offsetof…)` (a mismatch *cannot compile*),
  and `validate_descriptor` runs uniform-layout checks at dylib load. The claimed silent divergence is
  impossible.
- **06-F4** (uniform contract undocumented) — refuted: `WgslFilterBase` auto-generates the uniform struct
  (f32-only); authors never hand-write layout, so there's no author-facing contract to mis-follow.
- **06-F7** (no missing-file/codec test) — refuted: `test_movie_decode_route.cpp:64-68` loads a non-existent
  path and asserts `!success` + `avf_open_failed` diagnostic; `test_hap_codec.cpp` covers unsupported-fourcc
  fallback.
- **06-F8** (WebcamIn won't compile non-macOS) — refuted as stated: `capture_source.h` has no AVF
  dependency and cmake conditionally builds it. *(Real residual captured above as a link-error
  cross-platform note.)*
- **06-F9** (grace-period undocumented) — refuted: an 8-line comment block precedes `DeferredGpuRelease` /
  `kGpuReleaseGraceFrames` explaining the in-flight-command-buffer rationale + the `EXC_BAD_ACCESS` failure
  mode.
- **06-F10** (surface-suppression not inline) — refuted: `discard_frame()` / `end_frame()` already comment
  the resize/fullscreen/drag-tracking transitions inline.

## Completion Criteria

- [x] Findings table is filled in or explicitly marked with no findings.
- [x] Resource lifetime findings identify the owning object and cleanup path. *(No lifetime defects;
  ownership/cleanup confirmed: `GpuContext` owns WebGPU handles; `FrameExecutor` owns deferred texture
  release.)*
- [x] Media and GPU findings are separated when their fixes belong to different owners.
- [x] Platform-specific risks are labeled as macOS-only or cross-platform blockers.
- [x] Follow-up work is grouped into immediate, near-term, and backlog.
