# Phase 2: Realtime Audio And Thread Safety

Status: proposed

## Purpose

Verify that audio execution, transport, plugin hosting, analysis, and shared state satisfy the
release bar for realtime safety and thread correctness.

## User Task

Play, edit, analyze, and recover from audio/plugin activity without dropouts, deadlocks, data races,
or unsafe callback behavior.

## Hypothesis

If realtime boundaries are respected, release users can trust Vivid as an instrument rather than a
fragile demo.

## Pressure Test

Audit audio callback paths, graph mutation, note/event queues, analysis rings, plugin scan/hosting,
transport synchronization, and TSAN-relevant shared state.

## Scope

- Audio callback, audio graph, transport, note/event buses, clip DSP, sample engine, plugin hosting,
  analysis rings, movie audio bus, and UI/control interactions with audio state.
- Thread creation and ownership that can touch audio state.
- Sanitizer builds and stress tests relevant to realtime safety.

Out of scope: subjective audio quality unless a code issue causes instability, clipping hazards, or
incorrect timing.

## Audit Procedure

1. Trace the audio callback from device entry to operator/plugin/sample execution and back.
2. List every operation on the realtime path that could allocate, lock, block, do file IO, call UI
   code, or cross into plugin code.
3. Trace graph mutation and transport edits while playback is running.
4. Review queues, atomics, rings, snapshots, and lifetime ownership for analysis and note/event
   data.
5. Run existing audio tests, production-gate slices, and sanitizer targets where practical; note
   missing coverage explicitly.

## Evidence To Collect

- Realtime path trace with risky operations marked.
- Shared-state inventory: owner thread, reader threads, synchronization primitive, and tests.
- Test/sanitizer command summaries.
- Reproduction steps for any dropout, deadlock, race, or unsafe plugin behavior.

## Deliverables

- Realtime safety report.
- Thread-safety risk table with severity.
- Test gaps that must be closed before release.

## Acceptance Criteria

- Audio callback paths avoid allocation, blocking locks, file IO, and UI dependencies.
- Cross-thread state handoff uses explicit queues, atomics, or owned snapshots.
- Plugin failures cannot crash or corrupt the host project.
- Transport and note timing remain deterministic under ordinary editing.
- TSAN or focused stress tests cover the highest-risk shared-state paths.

## Failure Modes

- UI edits mutate audio-owned state directly.
- Plugin scanning or hosting runs unsafe work on the realtime path.
- Analysis buffers race with render or agent inspection.
- Timing bugs only appear under sustained playback or project reload.

## Evidence Log

- Pending.

## Open Questions

- What is the first-release audio performance budget?
- Which plugin formats are release-supported versus experimental?
- Which sanitizer targets are expected to pass before a release candidate is approved?

## Follow-Up Plans

- Link sanitizer runs, stress tests, callback fixes, and plugin-hosting decisions here.
