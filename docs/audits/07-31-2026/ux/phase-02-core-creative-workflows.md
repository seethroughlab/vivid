# Phase 2: Core Creative Workflows

Status: proposed

## Purpose

Verify that the main creative loop works end to end: start a project, make sound, make visuals,
connect them, iterate, save, close, reopen, and continue.

## User Task

Complete a small audiovisual sketch using only release-candidate affordances and bundled examples.

## Hypothesis

If the core loop is healthy, a motivated first user can reach a satisfying result without developer
intervention or undocumented setup.

## Pressure Test

Run scripted walkthroughs for new project creation, example loading, audio graph editing, visual
graph editing, mapping, playback, save/reopen, and export or recording where applicable.

## Scope

- New project, open project, save, save as, autosave-visible behavior, and reopen.
- Bundled examples and demos that are candidates for release.
- Session view, audio graph, visual graph, bridge/mapping controls, transport, preview, and export
  or recording paths.
- Undo/redo and recovery for ordinary creative mistakes.

Out of scope: exhaustive plugin compatibility, performance profiling, or internal test coverage
except where a workflow cannot be verified manually.

## Audit Procedure

Run three walkthroughs and keep notes at each decision point.

1. Blank sketch: start from the first-run state, create a sound source, create a visual response,
   map audio to visuals, play, stop, save, reopen, and confirm the result.
2. Example remix: open a release candidate example, identify the audio source, identify the visual
   graph, make one audible edit, make one visible edit, undo each, redo each, save a copy, and
   reopen it.
3. Failure-aware edit: intentionally attempt one unavailable, invalid, or mistaken operation and
   confirm the UI explains how to recover.

For each walkthrough, record the happy path, the first point of confusion, and the first point where
a user might need outside help.

## Evidence To Collect

- Task transcript with time-to-first-sound, time-to-first-visual, and time-to-first-mapping.
- Screenshots of start, mid-edit, mapped playback, saved/reopened, and recovery states.
- List of commands or controls that were required but not discoverable.
- Save/reopen diff notes: what changed, what stayed stable, and what was ambiguous.

## Deliverables

- Workflow scorecard for blank sketch, example remix, and failure-aware edit.
- Release-blocking workflow bugs with reproduction steps.
- Tutorial or docs gaps discovered by the walkthroughs.

## Acceptance Criteria

- Every primary command has a discoverable UI path.
- Playback, transport, and visible state stay synchronized.
- Save/reopen preserves user-created audio, visual, and mapping state.
- The user can recover from ordinary mistakes with undo or explicit reset paths.
- The workflow produces evidence suitable for release notes or a tutorial.

## Failure Modes

- A required workflow only works from tests, CLI, or developer memory.
- The app reaches an ambiguous state with no visible next action.
- Saving or reopening changes the creative result.
- Undo, selection, or focus state breaks the user's sense of control.

## Evidence Log

- Pending.

## Open Questions

- Is export or recording part of the first release bar, or a documented scaffold?
- Which bundled example should be treated as the canonical new-user walkthrough?
- What is the smallest satisfying audiovisual sketch for release validation?

## Follow-Up Plans

- Link workflow bugs, tutorial requirements, or missing command decisions here.
