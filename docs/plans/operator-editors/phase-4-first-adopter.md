# Phase 4: First Adopter - DrumSequencer

## Goal

Adopt the editor-window stack for DrumSequencer and use that operator as the proof that the full architecture works. The inspector remains intact as a compact quick-edit surface, while the dedicated editor becomes the larger authoring view.

This phase intentionally keeps scope narrow. It proves the infrastructure with one concrete operator before MSEG or other complex editors are migrated.

## Current Repo Facts

- The operator entry point is `operators/control/drum_sequencer/drum_sequencer.cpp`.
- Shared operator state and inspector method declarations live in `operators/control/drum_sequencer/drum_sequencer_core.h`.
- The current custom inspector drawing lives in `operators/control/drum_sequencer/drum_sequencer_inspector.cpp`.
- `cmake/operators.cmake` currently wires `drum_sequencer_au` to:
  - `drum_sequencer.cpp`
  - `drum_sequencer_core.cpp`
  - `drum_sequencer_inspector.cpp`

These are the real files that the implementation must update.

## Locked Decisions For This Phase

1. Add `VIVID_EDITOR(DrumSequencer)` to the real entry file.
2. Add `editor_metadata()` and `draw_editor(...)` declarations to `drum_sequencer_core.h`.
3. Keep the inspector UI intact.
4. Extract shared drawing and interaction helpers from `drum_sequencer_inspector.cpp` into real helper files compiled by `drum_sequencer_au`.
5. Keep editor v1 focused on the core pattern grid and keyboard navigation.
6. Defer copy/paste and MSEG migration to follow-up work.

## Editor Behavior For V1

The DrumSequencer editor should provide:

- a larger surface than the inspector
- trigger lane plus Mod A and Mod B visible simultaneously
- click to toggle triggers
- click-drag editing for modulation values
- arrow-key cursor movement
- `Enter` to toggle the selected trigger cell
- `Space` to clear the current or selected step

The plan should explicitly defer:

- `Cmd+C` / `Cmd+V` step copy-paste
- pattern-bank management
- editor-side preset browser
- MSEG migration

## Shared UI Extraction

Refactor the current inspector code so layout and interaction helpers can be reused by both surfaces.

Recommended structure:

- keep inspector-specific layout and small-surface policy in `drum_sequencer_inspector.cpp`
- extract reusable grid drawing and interaction helpers into one or two new files, for example:
  - `drum_sequencer_editor_shared.h`
  - `drum_sequencer_editor_shared.cpp`

The exact filenames may differ, but they must be real files compiled by `drum_sequencer_au` through `cmake/operators.cmake`.

The helper layer should encapsulate:

- lane layout math
- shared cell drawing
- common color and label logic
- trigger toggle behavior
- modulation drag behavior
- optional cursor-state helpers for editor keyboard navigation

Do not over-abstract this into a framework. The goal is to share concrete DrumSequencer UI logic between two surfaces.

## Operator Contract Changes

### `operators/control/drum_sequencer/drum_sequencer.cpp`

Add:

```cpp
VIVID_EDITOR(DrumSequencer)
```

alongside the existing registration macros.

### `operators/control/drum_sequencer/drum_sequencer_core.h`

Add:

```cpp
static VividEditorMetadata editor_metadata();
void draw_editor(VividEditorContext* ctx);
```

Add only the minimum extra persistent state required for editor interaction, such as keyboard cursor position, if that state truly must survive across frames.

### `operators/control/drum_sequencer/drum_sequencer_inspector.cpp`

Refactor this file to call shared helpers where practical, but preserve current inspector ergonomics. The inspector remains the compact surface and should not be forced into editor-sized assumptions.

## Editor Metadata

The docs should require DrumSequencer to provide reasonable defaults and minimums. Example values are acceptable as guidance, but the important requirement is behavioral:

- default size must be large enough to show all three lanes comfortably
- minimum size must still allow usable interaction
- title suffix should clearly identify the window as the DrumSequencer editor

## Build Wiring

Any new DrumSequencer translation units must be added in `cmake/operators.cmake` under the existing `drum_sequencer_au` target.

The phase must not mention:

- `drum_sequencer.h`
- a per-operator `CMakeLists.txt`

because neither exists in the current tree.

## Sync And Command Path

All editor-originated param writes must go through `ctx->commands`, which routes through `UICommandSink` and `RuntimeCommandSink`.

This requirement exists to preserve:

- undo capture
- coalescing behavior
- the same mutation path the inspector already uses

Do not bypass that path with direct runtime calls.

## Tests

### Automated Coverage

Add coverage that does not depend on driving a second native window:

1. Helper-level tests for shared grid interaction to param-name mapping.
2. Keyboard navigation tests for cursor movement and toggle semantics where logic is factored into testable helpers.
3. Build coverage ensuring new helper translation units are included in the operator target.

If second-window automation does not exist yet, do not promise it in this phase.

### Manual QA

1. Open DrumSequencer’s editor from the inspector and from the keyboard shortcut.
2. Verify the editor shows a larger, simultaneous view of trigger, Mod A, and Mod B lanes.
3. Edit from the editor and confirm the inspector updates immediately.
4. Edit from the inspector and confirm the editor updates immediately.
5. Verify arrow keys, `Enter`, and `Space` are captured by the editor when focused and do not leak into the main graph editor.
6. Rebuild the operator package and confirm the editor closes cleanly on the reload boundary.

## Acceptance Criteria

1. DrumSequencer exports `VIVID_EDITOR`.
2. The editor opens in a dedicated window and presents a larger authoring surface than the inspector.
3. Trigger plus Mod A plus Mod B are visible simultaneously in the editor.
4. Editor edits and inspector edits stay in sync.
5. Editor keyboard interactions work only while the editor has focus.
6. All editor mutations go through `ctx->commands`.
7. Reloading or rebuilding closes the editor cleanly.

## Follow-Up Work After This Phase

Create separate follow-up plan docs for:

- MSEG migration
- step copy-paste
- richer sequencer editing workflows
- any extension to second-window automated test coverage

Do not fold those into the initial DrumSequencer adoption.
