# Audit 05: Audio, MIDI & Plugin Hosting

**Date:** 2026-06-26
**Status:** Audited 2026-06-05 (verify-gated; 10 candidates → 6 confirmed, 4 dismissed)

## Purpose

Audit the audio runtime, MIDI integration, and native plugin-hosting paths for real-time safety, thread-boundary correctness, device lifecycle reliability, and plugin discovery risks.

## Scope

- `src/runtime/audio/`
- `docs/runtime/audio_engine.md`
- Audio-related parts of `src/runtime/graph/`
- Audio, MIDI, AU, VST3, and CLAP operators
- Relevant shared code under `operators/shared/`
- Audio, operator, lane, and integration tests that exercise audio cadence or plugin hosting

## Primary Questions

- [ ] Are real-time audio paths free of blocking operations, unbounded allocation, and unsafe locks?
- [ ] Are audio/frame bridge snapshots coherent and race-safe?
- [ ] Are device start, stop, switch, and failure paths recoverable?
- [ ] Are MIDI message lifetimes and routing semantics explicit?
- [ ] Are AU/VST3/CLAP scan and host boundaries clear and failure-tolerant?
- [ ] Do audio operators consistently handle sample rate, block size, lanes, and reset behavior?
- [ ] Are audio tests strong enough without relying on fragile device availability?

## Subsystem Checklist

- [ ] Trace audio callback execution and identify every dependency it touches.
- [ ] Review `AudioFrameBridge` usage from both audio and frame cadences.
- [ ] Inspect MIDI input/output and clock routing for timestamp and ownership assumptions.
- [ ] Review plugin scanning and host shared code for UI-thread, audio-thread, and background-thread boundaries.
- [ ] Check audio operators for denormal, clipping, reset, bypass, and preset consistency.
- [ ] Verify tests cover reload during audio processing, multi-lane audio, missing devices, and plugin scan failures.
- [ ] Identify places where plugin-specific code duplicates shared host behavior.

## Audit Checklist

- [ ] Read the relevant subsystem docs and navigation guides.
- [ ] Inspect the main source files and ownership boundaries.
- [ ] Review tests that claim to cover the subsystem.
- [ ] Check docs/code/test contract drift.
- [ ] Identify correctness, robustness, and maintainability findings.
- [ ] Record findings with severity, category, evidence, and recommendation.
- [ ] Propose immediate, near-term, and backlog follow-up work.

## Findings

**Real-time safety: CLEAN.** The audit's top-priority question — is the audio callback free of
allocation / locks / blocking / unbounded work — is a clear **yes**: `audio_executor.cpp` audio_callback
uses only pre-allocated buffers (lane-lift groups, LoopBased scratch allocated in `build()`), a lock-free
SPSC recording ring, and stack-only timing. Device lifecycle (start/stop/switch/disappearance/fallback/
rate-mismatch) is well-managed and recoverable. The `AudioFrameBridge` double-buffering is race-safe
(single audio-thread writer, main-thread reader; atomic acquire/release). **No high-severity findings.**

All 6 confirmed findings are **Low** — test-gaps and one maintainability item. Both candidates filed as
Medium (05-F3 device-switch test, 05-F7 rate-mismatch test) were **downgraded to Low** by the verify pass
(the operations are main-thread + synchronized by miniaudio / partially covered already).

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| 05-F6 | Low | Test gap / observability | Recording-tap ring overrun drops samples silently; `recording_overrun_count_` has no public accessor and the drop path is untested | `src/runtime/graph/audio_executor.cpp:1088-1100`, `audio_executor.h:182` |
| 05-F10 | Low | Robustness / test gap | MIDI input callback `push_back`s with no bounds check or `try/catch`; overflow/exception paths untested | `src/runtime/audio/system_midi.cpp:65-81` |
| 05-F5 | Low | Test gap | Plugin (VST3/AU/CLAP) scan failures are silently skipped (no per-plugin diagnostic) and untested | `src/runtime/audio/vst3_scanner.{h,cpp}`, `au_scanner.{h,cpp}` |
| 05-F7 | Low | Test gap (device-dependent) | Device-rate-mismatch detection (`pending_session_sample_rate_`) is untested on the AudioEngine side | `src/runtime/audio/audio_engine.cpp:60-64,165-170` |
| 05-F3 | Low | Test gap (device-dependent) | No test for `restart_device()` (device switch) during live audio processing | `tests/audio/test_audio_engine.cpp` |
| 05-F9 | Low | Maintainability | VST3/AU/CLAP host headers duplicate base64 codec + per-path ref-count maps + state/event handling across the three standards | `operators/shared/{vst3_host,au_host,clap_host}/*host_common.h` (vst3 = 1127 lines) |

### Evidence & Recommendation

**05-F6 — Recording-tap overrun is silent + unobservable + untested** (Low)
- *Evidence:* `audio_executor.cpp:1088-1100` — when the lock-free SPSC ring is full, the chunk is dropped
  and `recording_overrun_count_++` with a *one-time* stderr log (gated on `== 0`). `recording_overrun_count_`
  (`audio_executor.h:182`) is **private with no accessor** (only `available_recorded_samples()` /
  `pop_recorded_samples()` are public). The only recording test (`test_audio_capture_smoke.cpp`)
  deliberately throttles to *avoid* overrun, so the drop branch never runs. (Ring is 60 s @ 48 k stereo
  per `snapshot_types.h:70`, not the "10 s" in the stale comment — overrun is hard but reachable.)
- *Impact:* Sustained slow drain silently truncates the recorded WAV with no further signal. Low (a
  non-RT recording feature).
- *Recommendation:* Expose `recording_overrun_count()` on `AudioEngine`/executor; add a device-free test
  that fills the ring without draining and asserts the count increments. **Small, worth doing.**

**05-F10 — MIDI callback is unbounded / no exception guard** (Low)
- *Evidence:* `system_midi.cpp:65-81` — the rtmidi callback does `event_buffer_.push_back(...)` under a
  mutex with no size cap and no `try/catch`. Drained every frame via `drain_cc_events()` (CC-only, sysex/
  timing ignored), so in practice it self-limits (~16 events/drain at 60 Hz). Untested for overflow/throw.
- *Impact:* Only matters if the frame thread stalls while CC floods, or under OOM. Marginal.
- *Recommendation:* Add a defensive cap (drop-oldest beyond e.g. 4096 buffered) + a `try/catch` around the
  push; a small unit test of the drain/swap path. **Small.**

**05-F5 — Plugin scan failures silently skipped, untested** (Low)
- *Evidence:* `vst3_scanner.cpp` / `au_scanner.cpp` use `std::call_once` (no-op after first scan). All error
  paths swallow failures with no diagnostic: VST3 `opendir`/`stat`/`fopen(Info.plist)` failures silently
  skip; AU `AudioComponentCopyName != noErr` / empty-name silently skip. No per-plugin error capture; no
  test. *(Correction: the runtime VST3 scanner does **not** `dlopen` — it parses `Info.plist` via
  `fopen`/`fgets`; the header comment's "+ dlopen" is misleading.)*
- *Impact:* A broken bundle is silently omitted from the chooser with no user-facing reason. Low
  (degradation, not crash) — but a real diagnosability gap.
- *Recommendation:* Collect per-plugin scan errors and expose them (so the UI can show "N plugins skipped:
  …"); add scanner failure-path tests (corrupted bundle, unreadable dir).

**05-F7 — Device-rate-mismatch detection untested (device-dependent)** (Low)
- *Evidence:* `audio_engine.cpp:60-64,165-170` set `pending_session_sample_rate_` when the opened device's
  actual rate ≠ graph rate; `consume_pending_session_sample_rate()` drains it (consumer: `main.cpp:3212`).
  The AudioEngine detect/consume half is untested — null-device tests skip it (`audio_engine.cpp:60`). The
  *downstream* rebuild half is already covered by `test_runtime_core.cpp` Test 10 (audit 03-F10).
- *Impact:* Deterministic, hard-to-unit-test glue (needs a device mock reporting a wrong rate). Low.
- *Recommendation:* If a device-rate mock harness is added later, assert detect→consume; otherwise leave
  documented as a device-dependent gap.

**05-F3 — No device-switch-under-load test (device-dependent)** (Low)
- *Evidence:* `restart_device()` (`audio_executor.cpp:315`) uninits/reinits the `ma_device`; invoked from
  `AudioEngine::tick()` (main thread) on an `audio_out` `device` param change. No test exercises it. The
  RT-safety of the switch is delegated to miniaudio's `ma_device_uninit/stop` (which block until in-flight
  callbacks drain) — so this is a coverage gap, **not** a use-after-free.
- *Recommendation:* Add a device-switch test if/when a device mock exists; low priority given the
  library-provided synchronization.

**05-F9 — Plugin-host duplication across standards** (Low, Maintainability)
- *Evidence:* `vst3_host_common.h` (1127 lines), `au_host_common.h`, `clap_host_common.h` each carry their
  own base64 codec (comments literally say "same as clap_host_common.h"), their own per-path ref-count map
  (`g_vst3_bundle_refs`/`g_clap_entry_refs`), and their own save/load-state. The base64 + ref-count
  triplication is trivially shareable; the API-specific state/event handling is inherently per-standard.
  *(Correction: the finding named `vst3_effect`/`au_effect` operators that don't exist — only
  `clap_effect` ships; each `*_host_common.h` is included by exactly one operator, so there's no
  within-standard duplication.)*
- *Recommendation:* Extract the shared base64 + ref-count-map helpers into `plugin_common/`. Leave the
  per-standard host logic as-is. Backlog.

### Test Gaps (device-dependent vs device-free)

**Device-free (unit-testable now):**
- Recording-tap overrun counter increments when the ring fills (05-F6).
- MIDI CC drain/swap + bounded-buffer behavior (05-F10).
- Multi-lane audio operator reset/bypass consistency across `InstancePerLane` instances.
- Denormal handling in DSP (no subnormal-performance tests).

**Device-dependent (need a device/plugin mock; document as such):**
- Device disappearance → fallback-to-Default during processing; `restart_device()` under load (05-F3).
- Device-rate-mismatch detect→consume→recompile, AudioEngine side (05-F7).
- VST3/AU/CLAP scan failures: corrupted bundles, permissions, missing files (05-F5).
- AU/VST3/CLAP host instantiation/error paths (no dedicated host unit tests today).

### Docs to Update
- `docs/runtime/audio_engine.md` — already documents the per-thread bridge ownership + tick order; add an
  explicit "device lifecycle" subsection (start/restart/disappearance/fallback/rate-mismatch). (Basis of
  dismissed 05-F8, which was refuted because the *concurrency* contract is already in the header +
  `core/CLAUDE.md`; only the device-lifecycle prose is thin.)

## Follow-up

**Immediate** — none. No RT-safety / correctness defect.

**Near-term** — ✅ **DONE 2026-06-05** (build + tests green)
- 05-F6: `recording_overrun_count()` now public on `AudioExecutor` + `AudioEngine`; device-free overrun
  test added (`test_audio_engine.cpp` Test 6.5 — fills the ring undrained, asserts count > 0).
- 05-F10: MIDI callback now caps `event_buffer_` at `kMaxBufferedCcEvents` (drop-newest, counted via
  `dropped_cc_events()`) and is wrapped in `try/catch` so no exception escapes the RtMidi thread.

**Backlog / deferred**
- 05-F5: collect & surface plugin-scan errors — **deferred**: the scanners aren't path-injectable
  (scan fixed system dirs; AU uses Core Audio), so the failure paths aren't cleanly unit-testable
  without a scanner refactor. Pure diagnosability; revisit with that refactor.
- 05-F9: hoist base64 + ref-count helpers into `plugin_common/` — **deferred** (maintainability
  refactor across plugin-host headers; rebuilds plugin operators).
- 05-F3 / 05-F7: **deferred** — device-dependent; need a device-rate/switch mock harness.
- Work the device-free test gaps (lane reset/bypass, denormals).
- Add the audio_engine.md device-lifecycle subsection.

### Dismissed (verification-refuted)

Four candidates were refuted:

- **05-F1** (bridge "fragile TOCTOU") — refuted: the finder had the thread model **backwards**.
  `analysis_write_buffer`/`publish_analysis` run only on the single audio (producer) thread; the index
  can't change between loads, so there is no TOCTOU. Cosmetic at most.
- **05-F2** (device-disappearance refresh not throttled) — refuted: notifications coalesce to one
  `atomic<int>` (`exchange`), `tick()` processes ≤1/frame, and macOS interruption events are explicitly
  filtered out — the cited spam source can't even trigger the path.
- **05-F4** (lane-overflow untested/unalarmed) — refuted: `lane_overflow_count()` **is** wired into
  `runtime_health` (emits a Warning finding + JSON field) **and** asserted by
  `test_runtime_health_snapshot.cpp:227`. Only a narrow bridge-unit test is missing.
- **05-F8** (bridge concurrency undocumented) — refuted: the contract is documented in
  `audio_frame_bridge.h:15-21` + `core/CLAUDE.md` tick order; the alleged TOCTOU doesn't exist.

## Completion Criteria

- [x] Findings table is filled in or explicitly marked with no findings.
- [x] Real-time safety findings are called out with high priority. *(None — RT path audited clean;
  stated explicitly above.)*
- [x] Thread-boundary assumptions are documented or flagged. *(Audio-thread-only writers, main-thread
  readers, miniaudio-synchronized device ops — noted per finding; bridge contract confirmed documented.)*
- [x] Audio test gaps distinguish device-dependent and device-free coverage.
- [x] Follow-up work is grouped into immediate, near-term, and backlog.
