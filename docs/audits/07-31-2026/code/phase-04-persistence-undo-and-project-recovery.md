# Phase 4: Persistence, Undo, And Project Recovery

Status: proposed

## Purpose

Verify that user work survives ordinary editing, save/load cycles, crashes, autosave, undo/redo, and
project format evolution.

## User Task

Create a project, make layered edits, undo and redo changes, save, reopen, simulate recovery, and
confirm the audiovisual result is preserved.

## Hypothesis

If persistence and recovery are healthy, the first release will protect user trust even when the app
or an operator fails.

## Pressure Test

Audit project serialization, undo capture, autosave, crash snapshots, quarantine handling, schema
compatibility, and file action error paths.

## Scope

- Project file format, serialization/deserialization, undo manager, edit gateway, file actions,
  autosave, crash recovery snapshots, quarantine, package/operator references, and example project
  compatibility.
- State produced by audio graph, visual graph, mappings, clips, transport, plugin params, and
  package metadata.

Out of scope: long-term migration framework beyond the first-release compatibility promise.

## Audit Procedure

1. Build a release-supported state inventory and identify where each item is authored, stored,
   serialized, and restored.
2. Run round-trip tests for representative projects: blank sketch, bundled example, package-backed
   graph, and plugin-using project if release-supported.
3. Trace undo/redo capture through representative edits across audio, visual, mapping, and project
   metadata.
4. Simulate or inspect failure paths: save failure, load failure, crash snapshot, autosave recovery,
   missing package/operator, and unknown fields.
5. Compare persisted state with agent/control-server-visible state to catch hidden divergence.

## Evidence To Collect

- State inventory table with source of truth, serializer, undo behavior, and tests.
- Round-trip command summaries or manual transcripts.
- Before/after project snippets for representative edits if useful.
- Recovery scenario notes with user-visible outcomes.

## Deliverables

- Persistence coverage report.
- Undo/recovery risk list with release severity.
- Compatibility and migration notes for release examples and user projects.

## Acceptance Criteria

- One source of truth is serialized for every release-supported creative object.
- Undo/redo records user-intent operations, not accidental implementation side effects.
- Save/load round trips are covered by tests for representative audio, visual, bridge, and package
  state.
- Recovery flows never overwrite the user's last known good project without consent.
- Unknown or future project fields fail gracefully.

## Failure Modes

- Round trips preserve structure but change audible or visible output.
- Undo misses hidden state or restores stale pointers.
- Autosave/recovery creates competing versions without explaining them.
- Schema changes break existing examples or user projects.

## Evidence Log

- Pending.

## Open Questions

- What project format compatibility promise is made at first release?
- Which project states must be covered by golden round-trip tests?
- How should users choose between autosave, crash snapshot, and last manual save?

## Follow-Up Plans

- Link persistence tests, schema decisions, recovery UX fixes, and migration notes here.
