# ADR-0031: Audio Realtime Trust Gate

Status: proposed

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

