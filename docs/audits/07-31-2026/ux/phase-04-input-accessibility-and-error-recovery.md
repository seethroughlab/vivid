# Phase 4: Input, Accessibility, And Error Recovery

Status: proposed

## Purpose

Verify that users can operate the release candidate confidently with mouse, keyboard, text entry,
menus, file dialogs, plugin windows, and error states.

## User Task

Perform common edits and recover from common mistakes without needing to restart the app or inspect
logs.

## Hypothesis

If input and recovery are release-ready, Vivid will feel resilient even when users explore the edges
of the interface.

## Pressure Test

Exercise selection, dragging, typing, popup/menu navigation, keyboard commands, undo/redo, file
open/save failures, plugin scan failures, bad operator loads, and crash recovery surfaces.

## Scope

- Pointer selection, dragging, canvas gestures, menus, popup menus, text entry, keyboard commands,
  file dialogs, transport controls, plugin windows, toast/error surfaces, autosave, and crash
  recovery.
- Accessibility basics: readable contrast, target size, keyboard reachability, focus visibility,
  motion/flash hazards, and screen text clarity.
- Recoverability of common user mistakes and expected external failures.

Out of scope: full assistive-technology certification unless it becomes a release requirement.

## Audit Procedure

1. Build an input matrix covering mouse, trackpad-like dragging, keyboard commands, text entry, and
   menu navigation for each primary view.
2. Run conflict tests: type while transport shortcuts exist, drag while selection changes, open
   popups while playback runs, and switch focus while edits are pending.
3. Trigger recoverable failures: invalid file path, missing asset, bad operator/package, failed
   plugin scan, and simulated crash recovery if available.
4. Inspect whether errors appear where the user is already looking and whether they suggest a next
   action.
5. Run accessibility spot checks on contrast, target size, focus visibility, text truncation, and
   keyboard-only reachability.

## Evidence To Collect

- Input matrix with pass/fail/needs-follow-up for each region and modality.
- Failure transcript: action, visible response, recoverability, and logs if relevant.
- Screenshots of focus, warning, error, and recovery states.
- Accessibility notes with concrete UI references.

## Deliverables

- Input conflict list with severity and reproduction steps.
- Error/recovery inventory with release action.
- Accessibility spot-check report.

## Acceptance Criteria

- Keyboard and pointer interactions do not conflict across active UI regions.
- Text entry captures and releases focus predictably.
- Error messages are visible, actionable, and do not silently disappear.
- Crash recovery, autosave, and quarantine states are understandable to a user.
- Basic accessibility checks cover contrast, readable text, target size, and keyboard reachability.

## Failure Modes

- A focused text field leaks commands to the app.
- Failure states appear only in logs.
- Undo/redo changes hidden state without visible confirmation.
- Plugin or operator failures leave the project half-loaded or unrecoverable.

## Evidence Log

- Pending.

## Open Questions

- Which keyboard shortcuts are official release behavior?
- What errors are allowed to be developer-log-only during first release?
- What accessibility bar is required for the first public build?

## Follow-Up Plans

- Link input bugs, accessibility notes, and recovery-flow fixes here.
