# Audit 06: GPU, Media & Visual I/O

**Date:** 2026-06-26
**Status:** Re-audited (maintainability) 2026-06-05 (verify-gated; 5 candidates → 4 confirmed, 1 dismissed). Prior correctness pass retained below; Round-2 maintainability section at end.

## Purpose

Audit GPU runtime services and visual media paths for resource lifetime, shader contract, platform interop, capture/readback, and media decode robustness risks.

## Re-Audit Mandate

The prior pass should be treated as a correctness/robustness audit, not a complete code-quality audit.
Run this audit again with equal weight on maintainability: structure, duplication, ownership boundaries,
API clarity, dependency direction, and ease of future change.

Do not mark the audit complete until every checklist item is annotated as `[x]` done, `[~]` partially
covered, or `[ ]` intentionally deferred with a short note. Findings must include both confirmed defects
and structural risks that make future defects likely.

## Scope

- `src/runtime/gpu/`
- `docs/runtime/gpu.md`
- GPU and media-related operators under `operators/gpu/`
- Movie decode/session/upload shared code under `operators/shared/`
- Webcam, Syphon, texture loader, movie, capture, and analysis paths
- GPU, media, operator, and integration tests

## Primary Questions

- [x] Are WebGPU, Metal, texture, and buffer lifetimes explicit and leak-resistant? → Clean; no resource-lifetime defect confirmed.
- [x] Are WGSL parsing and uniform layout rules consistent between runtime and operators? → Covered; helper docs clarified when to use standard GPU helpers versus hand-rolled paths.
- [x] Do media decode/upload/session paths handle reload, seek, failure, and shutdown safely? → Mostly yes; decoder-concrete-type cleanup remains deferred with rationale.
- [x] Are visual I/O operators resilient to missing devices, missing files, and unsupported codecs? → Covered; WebcamIn non-macOS guard and movie diagnostic follow-up addressed the actionable gaps.
- [x] Are capture, screenshot, readback, and analysis paths synchronized correctly? → Covered; no confirmed synchronization defect.
- [x] Are GPU-backed lane or texture outputs documented and tested? → Partially; lane-related work is deferred to the lane-value clean-break.
- [x] Are platform-specific paths isolated enough for future cross-platform work? → Improved; WebcamIn now guards the macOS-only camera enumeration path.

## Subsystem Checklist

- [x] Trace GPU context initialization, resize, frame submission, and shutdown. → Covered; no confirmed lifetime defect.
- [x] Review WGSL header parsing and uniform layout assumptions against representative GPU operators. → Covered; helper/API docs updated for standard-vs-custom layouts.
- [x] Inspect movie decode worker/session/queue/upload lifetimes. → Covered; concrete decoder dependency remains backlog.
- [x] Check webcam and Syphon behavior for unavailable devices and platform stubs. → Covered; WebcamIn non-macOS guard added.
- [x] Review texture readback, screenshot, mipmap, and analysis utilities for synchronization hazards. → Covered; no confirmed defect.
- [x] Verify tests cover shader parse failures, missing media, reload during playback, and capture correctness. → Covered at existing integration level; remaining media/device gaps are Low/deferred.
- [x] Identify repeated GPU resource setup patterns that should become helpers or API contract docs. → Covered; `create_fallback_texture` extracted.

## Audit Checklist

- [x] Read the relevant subsystem docs and navigation guides.
- [x] Inspect the main source files and ownership boundaries.
- [x] Review tests that claim to cover the subsystem.
- [x] Check docs/code/test contract drift.
- [x] Identify correctness, robustness, and maintainability findings.
- [x] Identify oversized files, mixed responsibilities, fragile seams, and unclear ownership.
- [x] Identify duplicated logic or repeated patterns that should be shared or intentionally documented.
- [x] Check dependency direction and public/private API boundaries.
- [x] Check whether tests make future refactors safe, not just whether they cover the latest fix.
- [x] Record findings with severity, category, evidence, and recommendation.
- [x] Propose immediate, near-term, and backlog follow-up work.

## Required Maintainability Review

- [x] Map GPU context, frame execution, media decode, texture upload, readback, and platform-I/O responsibilities. → `src/runtime/gpu/` helpers + media-decode pipeline are coherent/well-factored; the only structural issue is duplicated operator placeholder logic.
- [x] Look for duplicated resource lifecycle, shader-layout, media diagnostic, placeholder, and platform-stub logic. → **the 1×1 fallback/placeholder texture is duplicated across 9 GPU ops** (06-R2-F1). Pipeline/bind-group "duplication" was refuted — per-operator-necessary, not factorable.
- [x] Check whether GPU/media APIs expose implementation detail or force operators to duplicate runtime behavior. → `movie_file.cpp` reaches **concrete decoder types** (`HAPDecoder*`/`AVFDecoder*`) for methods missing from the abstract `VideoDecoder` interface (06-R2-F3); `gpu_common.h` helpers under-documented on when to use vs hand-roll (06-R2-F5).
- [x] Check whether macOS-specific behavior is isolated from cross-platform abstractions. → mostly yes (Syphon correctly stubbed); **WebcamIn has no non-macOS stub** for `enumerate_cameras()` → blocks cross-platform compile (06-R2-F4).
- [x] Identify code that is correct today but fragile under likely GPU-output, media-codec, readback, or platform changes. → the 9× fallback-texture copies + the decoder-cast abstraction break; GPU lane-promotion is **deferred** to the lane-value clean-break.
- [x] Produce refactor candidates with priority and expected payoff, separate from bug fixes. → see Round-2 Refactor Candidates below.

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

---

# Maintainability Re-Audit (Round 2) — 2026-06-05

Verify-gated maintainability pass per the Re-Audit Mandate (round 1 found the subsystem clean on
correctness/resource-lifetime). **5 candidates → 4 confirmed (2 Medium, 2 Low), 1 dismissed.** The
`src/runtime/gpu/` helpers and the media-decode pipeline (factory → AVF/HAP → texture_upload →
queue/worker/transport) are **coherent and well-factored**; `particles_2d` is a justified unified modular
operator (not a god-file); and the broad "operators hand-roll pipelines" claim was correctly **not** a
finding (per-operator non-standard layouts are necessary, not duplication). The verify pass refuted the
`movie_file.cpp` "god-file" claim — it's actually decomposed into internal classes
(`MovieFileSession`/`MovieFileRing`/`MovieFileFillThread`), and several cited line ranges were wrong.

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| 06-R2-F1 | Medium | Maintainability | The **1×1 fallback/placeholder texture creation** (for a disconnected input) is duplicated across **9 GPU operators** — same 3-step pattern (`WGPUTextureDescriptor` 1×1 → view → `wgpuQueueWriteTexture` of zero); no `gpu_common.h` helper | `bloom.cpp:386`, `trails.cpp:562`, `composite.cpp:455`, `particles_2d.cpp:1306`, `feedback.cpp:271`, `mesh_warp.cpp:374`, `lut_apply.cpp:386`, `scopes.cpp:468`, `motion.cpp:275` |
| 06-R2-F3 | Medium | Maintainability | `movie_file.cpp` **breaks the `VideoDecoder` abstraction** — `static_cast<HAPDecoder*>`/`<AVFDecoder*>` to call methods absent from the interface (`video_decoder.h:20-56`) | `movie_file.cpp:732,752,830,993,1006,1025,1039` |
| 06-R2-F4 | Low | Robustness | **WebcamIn has no non-macOS stub** — `enumerate_cameras()` is defined only in `avf_capture.mm`, so non-macOS builds fail to link (unlike Syphon's `syphon_in_stub.cpp`) | `webcam_in.cpp:108`; `avf_capture.mm:339`; `capture_source.h:15` |
| 06-R2-F5 | Low | Docs | `gpu_common.h` `create_standard_bind_layout/group` doc describes the layout's shape but not **when to use it vs hand-roll** (so operators over-hand-roll) | `gpu_common.h:217-281` |

> 06-R2-F1 nuance: the 9 copies are the same *structural* pattern but not byte-identical — the 8 effect ops
> use `gpu->output_format` (RGBA16Float, transparent zero, 8-byte rows); `particles_2d` uses RGBA8Unorm /
> opaque black / 4-byte rows. So the extracted helper must **parameterize format + fill-value + row stride**.
> All findings are **non-lane** (GPU lane-promotion is deferred to the clean-break).

### Evidence & Recommendation

**06-R2-F1 — fallback-texture duplication ×9** (Medium, Maintainability — *the cheap, high-value fix*)
- *Evidence:* each op has a private `create_fallback(const VividGpuContext*)` doing the identical
  descriptor→view→write-zero sequence (verified across all 9). `gpu_common.h` has buffer/sampler/texture
  factories but no fallback-texture helper.
- *Recommendation (refactor candidate):* add
  `create_fallback_texture(WGPUDevice, WGPUQueue, WGPUTextureFormat, bool opaque_black = false)` →
  texture+view to `gpu_common.h`, parameterizing format + fill + stride; migrate the 9 ops.
  **Priority medium, payoff high, low-risk** — guarded by the existing GPU output tests (a disconnected
  input still renders the same), plus a small unit test of the helper.

**06-R2-F3 — `movie_file` decoder-abstraction break** (Medium, Maintainability)
- *Evidence:* `video_decoder.h:20-56` declares the abstract interface (open/close/decode_frame/pixel_data/…);
  `movie_file.cpp` `static_cast`s to `HAPDecoder*`/`AVFDecoder*` (7 sites) to call concrete-only methods
  (`make_decoded_frame`, AVF pixel-buffer helpers). The operator therefore knows each codec's concrete type.
- *Recommendation:* lift the needed operations onto `VideoDecoder` as virtuals (implemented by HAP/AVF) and
  drop the casts. **Priority medium, payoff medium** — restores the boundary; moderate (interface + 2
  decoders + the operator). Some AVF/HAP-specific scheduling may legitimately stay concrete — scope to the
  genuinely-shared operations.

**06-R2-F4 / F5 — small platform/doc items** (Low) — add an `enumerate_cameras()` non-macOS stub (build
includes it off-macOS, `avf_capture.mm` on macOS); expand the `gpu_common.h` helper doc with a "use this for
simple fragment ops; hand-roll for compute/multi-pass" note.

### Refactor Candidates (priority + payoff — separate from bug fixes)
1. **`create_fallback_texture()` helper + migrate 9 ops** (06-R2-F1) — **priority medium, payoff high,
   low-risk.** The clear win; behavior-neutral, test-guarded.
2. **Lift `movie_file`'s concrete decoder ops onto `VideoDecoder`** (06-R2-F3) — priority medium, payoff
   medium; restores the abstraction.
3. **WebcamIn non-macOS stub** (06-R2-F4) — priority low, payoff low (cross-platform compile).
4. **`gpu_common.h` helper "when to use" doc** (06-R2-F5) — trivial.

### Test Gaps (refactor-safety)
- Fallback-texture behavior (would unit-test the extracted helper); decoder-factory routing (HAP vs AVF by
  codec); the 17 metadata/telemetry ports. (Round-1 06-F5/F6 GPU-dependent test gaps remain deferred.)

### Dismissed (verification-refuted)
- **06-R2-F2** (`movie_file.cpp` 1412-line "god-file" mixing 7 concerns) — refuted: the file **is** large
  but **already decomposed** into internal classes (`MovieFileSession`, `MovieFileRing`,
  `MovieFileFillThread`) that separate the concerns; several cited line ranges were wrong. "Needs
  decomposition" over-reaches — the structure is reasonable for a media operator.

### Out of scope (lane-value clean-break)
- GPU lane-promotion (`kGpuLanePromotionThreshold`, `lane_input_gpu_promoted`, `lane_buffer_gpu`,
  `VividGpuContext.input_lane_gpu_*`) is rewritten by the queued clean-break (Phase 4) — not audited here.

## Round-2 Follow-up
- **DONE 2026-06-05 (06-R2-F1/F4/F5):** extracted `vivid::gpu::create_fallback_texture(device, queue,
  format, opaque_black)` into `gpu_common.h` and migrated all 9 ops (net −142 lines; behavior-neutral,
  parity-proven by `test_gpu_operators`/`test_gpu_correctness`/`test_demo_graphs`); guarded WebcamIn's
  `enumerate_cameras()` call with `#ifdef __APPLE__` (non-macOS link fix); documented the `gpu_common.h`
  bind-layout helpers' when-to-use. Merged (`674e886b`).
- **Deferred — backlog (06-R2-F3, with rationale):** `movie_file.cpp`'s concrete `HAPDecoder*`/`AVFDecoder*`
  casts reflect a **genuine sync-vs-async decode-model difference** (`make_decoded_frame` is HAP-only;
  `read_pixel_buffers_until`/`acquire_pixel_buffer` are AVF-only). Hoisting them onto `VideoDecoder` would
  make it a **fat interface** — worse than the honest casts. A clean fix needs a sync/async decode-model
  abstraction (moderate redesign). Documented the intentional split in `movie_file.cpp`; the redesign is
  deferred. Also backlog: the fallback-texture / decoder-routing / telemetry-port unit tests.
