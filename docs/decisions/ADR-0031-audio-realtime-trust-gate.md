# ADR-0031: Audio Realtime Trust Gate

Status: accepted (2026-08-09 — implemented across 5 slices; see As Built)

Date: 2026-07-26

Extends [ADR-0029](ADR-0029-concurrency-model-is-tsan-gated.md),
[ADR-0025](ADR-0025-cpp17-organization-and-patterns.md), and
[ADR-0019](ADR-0019-nothing-fails-silently.md).

## Context

Vivid already has a real realtime discipline: the audio callback avoids blocking and allocation,
session mutations happen on the UI thread, and cross-thread channels are documented in
`app/docs/thread-safety.md`. ADR-0029 added TSan-labelled concurrent tests for selected channels and
named the remaining work: a dedicated concurrent harness where a render thread races with UI-style
session edits, plus hardening the remaining plain-memory snapshot channels.

That is necessary, but professional audio trust needs one more step. Users do not only need "the
code avoids obvious races"; they need "the app tells us when realtime failed, and CI exercises the
failure modes before users do."

Today the engine has strong conventions and many headless tests, but it does not yet have a single
gate that answers:

- did the audio thread ever allocate, block, or take a contended lock on this path?
- did a UI edit race with rendering under TSan?
- did a block miss its budget?
- did a plugin/graph edit produce silence, stuck notes, or stale routing?
- did the app surface the failure in the same health/log vocabulary as other failures?

## Decision

Create an **audio realtime trust gate**: a required test and instrumentation layer focused on the
audio callback's actual risk surface.

1. **Add a concurrent session harness.** One thread repeatedly drives `session_process` with realistic
   block sizes while another performs UI-thread-style edits: clip changes, graph connects/disconnects,
   track add/remove, plugin placeholder loads, param drags, scene launches, mappings, and undo/redo
   where applicable. Run it under TSan as a `THREAD`-labelled test.

2. **Harden remaining snapshot channels.** Replace plain-memory concurrent snapshots for per-node
   scopes, per-node FFT rings, and held-note sets with atomic-slot or publish-copy designs. A
   tolerated one-frame visual tear is acceptable; C++ undefined behavior is not.

3. **Track callback health.** Add cheap audio-thread counters for callback duration buckets,
   oversized blocks, missed budget estimates, dropped queues, skipped `try_lock` handoffs, and render
   bail-to-silence events. These counters are atomics or lock-free rings only.

4. **Surface audio health through ADR-0019.** Realtime warnings feed `HealthSnapshot`, the diagnostics
   panel, and the log. Do not invent a new surface. Warning-level issues are passive; hard failures
   such as repeated render bailouts are errors.

5. **Make the production gate audio-aware.** The gate should run the audio graph, audio op RT,
   sampler, warp, clip, package-audio, analysis-ring, note-bus, and concurrent-session harness tests.
   The TSan leg must include every cross-thread channel added by future audio work.

6. **Define budgets in code, not prose.** A small config or header owns callback budget assumptions:
   max supported block size, expected sample rates, stress-test duration, and allowed skipped handoff
   counts. Tests and health reporting read the same values.

## Alternatives Considered

- **Rely on code review plus existing unit tests.** Rejected. Realtime bugs are emergent; the
  important failures happen while rendering and editing run concurrently.
- **Run the full GUI app under TSan.** Rejected as the main gate. It is slow, noisy, and pulls in
  third-party thread behavior. Keep it available for local investigation; make the required gate
  targeted and deterministic.
- **Add locks around contested data.** Rejected. Blocking the audio callback violates the existing
  realtime model. The fix is better publication, not broader locking.
- **Hide xrun/budget metrics until perfect.** Rejected. The numbers can start conservative and honest:
  "callback exceeded budget estimate" is useful if it is labelled as an estimate.

## Consequences

- **Positive:** Realtime safety becomes a checkable contract, not a heroic review exercise.
- **Positive:** Users and agents get truthful audio-health state when the engine is overloaded or a
  realtime handoff is repeatedly skipped.
- **Tradeoff:** The harness will expose bugs that ordinary tests miss; the first pass may be noisy.
- **Follow-up:** Once green, make the TSan audio trust gate required in branch protection and update
  `app/docs/thread-safety.md` so a new cross-thread channel is not complete until the harness drives it.

## As Built (2026-08-09 — 5 slices, PRs #315–#319)

Grounding the six decisions against the live tree at implementation time reshaped the work: **decision
#2 was already delivered by ADR-0029** and **#1 was mostly delivered**, so the net-new work was the
audio-health instrumentation (#3/#4/#6), the harness extension (#1 remainder), and the gate wiring (#5).

1. **#6 budgets in code — DONE (slice 1, #315).** `app/src/audio/audio_budgets.h`: `AudioBudgets` +
   cached `audio_budgets()` accessor (reads env once, warmed on the main thread like `watchdog_config()`).
   Owns max block / device period / sample rate / callback budget multiplier / bailout-Error count /
   stress-ms / allowed-skips. A `static_assert` in `vst3_host_internal.h` pins the max-block default to
   `kGraphMaxBlock`.
2. **#3 callback health counters — DONE (slice 2, #316).** `app/src/audio/audio_health.h`
   (`vivid::audio::health`): relaxed-atomic counters (callbacks, render bail-to-silence, over-budget,
   handoff skips) + last/max callback-µs gauges; `steady_clock` timing in `audio_callback.cpp`; the 8
   pure-contention `try_lock` sites in `session_process` credit a skip. **RT-scope gate decision:** a
   `thread_local` set only by `audio_callback` (`RtScope`/`in_rt()`) — chosen over threading a `bool`
   param through `session_process`'s public signature — so the offline bounce never ticks the metric.
   The capture / `aud_mtx` try_locks are **excluded** (they skip on "no work" as often as contention).
3. **#4 surface via HealthSnapshot — DONE (slice 3, #317).** `HealthSnapshot` gains the audio fields as
   per-frame deltas + gauges; `severity()` maps sustained render bailouts (≥ threshold) → Error and
   over-budget/skips → passive Warning, staying pure by taking the threshold as a snapshot field.
   **Empty-session bail exclusion:** only oversized-block bail counts, so an idle/empty session never
   rolls up to Error. Folded into the existing `HealthSnapshot`/`get_health` (no new MCP tool → parity
   test stays green) + a diagnostics-panel "Audio RT" row.
4. **#1 concurrent harness — DONE (slice 4, #318).** Extended `test_session_concurrency` to churn graph
   edges (connect/disconnect audio + control edges, remove nodes) and run the render thread in `RtScope`;
   budget-driven soak (`VIVID_AUDIO_STRESS_MS`) + opt-in skip ceiling. Clean under TSan. **Undo/redo and
   audio↔visual mappings are out of scope** for this harness: both are App/UI-layer (`undo_manager`,
   `MappingRegistry`), not the `session_*` C API — they race the edit model, not `session_process`.
5. **#5 production gate audio-aware — DONE (slice 5, #319).** Killed the curated `--target` foot-gun in
   `production-gate-pr.yml` (a new AUDIO_ENGINE test not added to the list went red): the
   `audio-engine-tests` job now builds all and selects by label, mirroring `run_release_verification.sh`.
   The portable `test_audio_budgets` + `test_runtime_health` additions ride the existing `core`
   (`HEADLESS_SMOKE`) gate; the app-ON audio tests run in the existing `audio-engine-tests` +
   `audio-thread-sanitizer` legs (no new `run_production_gate.sh audio` profile). `thread-safety.md`
   documents the counters, the RT-scope gate, and the extended harness.

**Manual follow-up (repo admin, not a file change):** make `audio-engine-tests` and
`audio-thread-sanitizer` *required* status checks in branch protection so the audio gate actually blocks
a merge — a GitHub setting, out of scope for these PRs.

