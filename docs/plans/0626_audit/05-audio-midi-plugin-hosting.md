# Audit 05: Audio, MIDI & Plugin Hosting

**Date:** 2026-06-26
**Status:** Re-audited (maintainability) 2026-06-05 (verify-gated; 4 candidates → 2 confirmed, 2 dismissed). Prior correctness pass retained below; Round-2 maintainability section at end.

## Purpose

Audit the audio runtime, MIDI integration, and native plugin-hosting paths for real-time safety, thread-boundary correctness, device lifecycle reliability, and plugin discovery risks.

## Re-Audit Mandate

The prior pass should be treated as a correctness/robustness audit, not a complete code-quality audit.
Run this audit again with equal weight on maintainability: structure, duplication, ownership boundaries,
API clarity, dependency direction, and ease of future change.

Do not mark the audit complete until every checklist item is annotated as `[x]` done, `[~]` partially
covered, or `[ ]` intentionally deferred with a short note. Findings must include both confirmed defects
and structural risks that make future defects likely.

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
- [ ] Identify oversized files, mixed responsibilities, fragile seams, and unclear ownership.
- [ ] Identify duplicated logic or repeated patterns that should be shared or intentionally documented.
- [ ] Check dependency direction and public/private API boundaries.
- [ ] Check whether tests make future refactors safe, not just whether they cover the latest fix.
- [ ] Record findings with severity, category, evidence, and recommendation.
- [ ] Propose immediate, near-term, and backlog follow-up work.

## Required Maintainability Review

- [x] Map audio runtime, bridge, MIDI, device, plugin-host, and shared-DSP responsibilities. → engine/device/MIDI/bridge ownership is clean and focused.
- [x] Look for duplicated host, scanner, state serialization, thread-boundary, diagnostic, and buffer-management logic. → the duplication is in the **plugin-instrument operators** (05-R2-F2), not the host headers (host-level dup is the known-deferred 05-F9; `clap_plugin_window.h` already shared — 05-R2-F1 refuted).
- [x] Check every cross-thread public accessor for atomics, locks, or explicit single-thread ownership. → **clean** — consistent atomic acquire/release + documented ownership on bridge + engine + the operator triple-buffer.
- [x] Check whether plugin-standard-specific code is separated from reusable host utilities. → reasonable: shared host headers + `plugin_common` (macro_bank/direct_param_queue/base64); the per-SDK glue lives in each operator (the 05-R2-F2 duplication).
- [x] Identify code that is correct today but fragile under likely audio-device, MIDI-flood, plugin, or lane changes. → the 4× plugin-operator lifecycle (a swap/cleanup fix must be replicated 4 times — 05-R2-F2); the `dying2_` overflow is a deliberate documented tradeoff (05-R2-F4 refuted).
- [x] Produce refactor candidates with priority and expected payoff, separate from bug fixes. → see Round-2 Refactor Candidates below.

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

---

# Maintainability Re-Audit (Round 2) — 2026-06-05

Verify-gated maintainability pass per the Re-Audit Mandate (round 1 found RT-safety clean). **4 candidates →
2 confirmed (1 Medium, 1 Low), 2 dismissed.** Low yield, as expected — the audio runtime/engine/MIDI/device
and the shared DSP libs are **clean and well-factored**, cross-thread accessors are consistent (atomic
acquire/release + documented ownership), and the plugin-host-header duplication is the known-deferred 05-F9.
The remaining maintainability story is the **plugin-instrument operators**. The verify pass refuted both Low
candidates: the `clap_plugin_window.h` "duplicate" (clap_instrument **already** `#include`s the shared copy —
`clap_instrument.cpp:10`) and a `dying2_` "handle leak" (a deliberate, documented don't-delete-on-the-audio-
thread tradeoff).

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| 05-R2-F2 | Medium | Maintainability | The **4 plugin-hosting operators duplicate** the triple-buffer plugin-swap (`active_/pending_/dying_/dying2_` atomics), the `main_thread_update` housekeeping (~40 LOC: cleanup dying → reload_if_changed → update_macro_map → refresh_*_params_json → process_direct_params → save_state), and the `process_audio` swap — ~90% textual match, no shared base | `vst3_instrument.cpp:83-86,180-232,496-528`; `clap_instrument.cpp:73-76,162-208,384-410`; `au_instrument.cpp:62-66,124-152,259-277`; `clap_effect.cpp:68-71,155-202,379-410` |
| 05-R2-F3 | Low | Test gap | The plugin-operator lifecycle glue (triple-buffer swap ordering, `main_thread_update` cleanup, macro-map persistence, state round-trip, direct-param-queue thread-safety) is **untested** — only `test_plugin_base64` exists | `tests/operators/test_plugin_base64.cpp` (only); the 4 operators |

> 05-R2-F2 is a real, well-evidenced duplication (au_instrument's own comment says *"triple-buffer, same
> pattern as CLAPInstrument"*). But it's a **deferred-grade fix**: the factorable portion is real
> (load/swap/cleanup), yet the SDK calls differ (VST3 `setProcessing` / CLAP `start/stop_processing` / AU
> destructor), so a `PluginOperatorBase<HandleType>` needs **virtual hooks on the audio-thread swap path**
> and there is **no real-plugin test harness** (CI has no installed plugins). 05-R2-F3 is the prerequisite
> safety net.

### Evidence & Recommendation

**05-R2-F2 — plugin-operator lifecycle duplication** (Medium, Maintainability — *the headline; deferred-grade fix*)
- *Evidence:* identical `active_/pending_/dying_/dying2_` atomic quad in all 4 ops (cited lines, verified);
  `main_thread_update` opens with the same dying/dying2 exchange+cleanup, then the `audio_rate_seen_`
  mismatch-reload block, then the same call sequence; `process_audio` does the same load-pending →
  atomic-exchange-into-active → stop-old → move-to-dying swap. Differences are SDK-specific (the
  processing-enable call + the destroy path).
- *Impact:* a fix to swap ordering / cleanup semantics / macro-map timing must be replicated **4×** in
  complex, non-obvious thread-handoff code — exactly where bugs hide.
- *Recommendation (refactor candidate — DEFERRED):* a `PluginOperatorBase<HandleType>` (template/CRTP to
  avoid audio-thread virtual dispatch where possible) encapsulating the triple-buffer lifecycle +
  `main_thread_update` cleanup + `process_audio` swap, with hooks for the per-SDK `start/stop_processing` and
  `destroy`. **Do it deliberately, gated by 05-R2-F3's mock-handle unit tests** — not a quick fix, and it
  touches 4 critical audio operators.

**05-R2-F3 — plugin-operator test gap** (Low, Test gap — *prerequisite for F2*)
- *Recommendation:* a device-free unit test with **mock handles** exercising the triple-buffer transitions
  (pending→active→dying, dying2 overflow), `main_thread_update` cleanup ordering, and direct-param-queue
  main→audio delivery. Real-plugin integration is impractical (no installed plugins in CI) and out of scope.

### Refactor Candidates (priority + payoff — separate from bug fixes)
1. **`PluginOperatorBase<HandleType>`** (05-R2-F2) — priority medium, **payoff high but cost/risk high**:
   removes ~90% duplication across 4 ops, but needs virtual/CRTP hooks on the RT swap path and a mock-handle
   test suite first. **Sequence: 05-R2-F3 test harness → then the base.** Deferred-grade.
2. **Mock-handle plugin-lifecycle unit tests** (05-R2-F3) — priority low, payoff medium; the standalone
   first step that makes #1 safe.

### Dismissed (verification-refuted)
- **05-R2-F1** (`clap_plugin_window.h` duplicated in `clap_instrument/`) — refuted: `clap_instrument.cpp:10`
  already `#include`s `"shared/clap_host/clap_plugin_window.h"`; the build uses the shared copy exclusively.
  (A stale unused copy under `operators/audio/clap_instrument/` may exist and could be deleted for hygiene,
  but it is **not an active duplication**.) *(Recon hypothesis — correctly killed.)*
- **05-R2-F4** (`dying2_` overflow silently leaks a handle, VST3) — refuted: it's a **deliberate, documented
  tradeoff** (`vst3_instrument.cpp:510-514` logs and accepts the rare leak rather than `delete` on the audio
  thread — a third swap without an intervening main-thread tick). Intentional, not a defect.

### Out of scope (lane-value clean-break)
- `audio_frame_bridge.cpp` lane-lift / lane storage is rewritten by the queued lane-value clean-break
  (Phase 5) — not audited for refactor here.

## Round-2 Follow-up
- **DONE 2026-06-05 (05-R2-F3):** extracted the triple-buffer swap/retire/reclaim mechanism verbatim into
  `operators/shared/plugin_common/plugin_slot.h` (`PluginSlot<HandleT>` — atomics + `swap_in_pending` with
  SDK-op callbacks + `reclaim`/`destroy_all`) and added `tests/operators/test_plugin_slot.cpp` (mock handle;
  5 scenarios incl. the 3-retire overflow-leak + invalid-handle rejection). The 4 operators are **not yet
  migrated** (zero RT-path risk) — this is the **safety net** that makes the F2 base extraction safe.
  Merged (`54462d94`).
- **Backlog — deferred-grade (05-R2-F2):** migrate the 4 plugin-hosting operators onto `PluginSlot<HandleT>`
  (the `PluginOperatorBase` step), each swap/main-update block → `slot_.swap_in_pending(...)` /
  `slot_.reclaim(...)` with the per-SDK lambdas. Now guarded by `test_plugin_slot`, but still a deliberate RT
  refactor of 4 critical operators with no real-plugin CI test — do it carefully, one operator at a time.
  Optional hygiene: delete the stale `operators/audio/clap_instrument/clap_plugin_window.h` copy.
