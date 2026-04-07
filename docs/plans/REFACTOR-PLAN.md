# Refactor Plan: Large File Decomposition

Audit date: 2026-04-02

## Summary

The core architecture is solid. The main maintenance pain is concentrated in a few oversized files, especially the UI layer around `NodeGraphUI`, plus two runtime files:

- [src/ui/node_graph_draw.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph_draw.cpp)
- [src/ui/node_graph_input.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph_input.cpp)
- [src/ui/node_graph.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph.cpp)
- [src/ui/node_graph.h](/Users/jeff/Developer/vivid/src/ui/node_graph.h)
- [src/runtime/main.cpp](/Users/jeff/Developer/vivid/src/runtime/main.cpp)
- [src/runtime/control_server.cpp](/Users/jeff/Developer/vivid/src/runtime/control_server.cpp)

This plan is intentionally execution-oriented. It breaks the work into smaller seams that can land safely, with clear ownership boundaries and acceptance criteria.

## Guiding Rules

These rules apply to every phase:

1. Preserve behavior unless the phase explicitly says otherwise.
2. Do not mix refactoring with unrelated feature work.
3. Prefer extracting cohesive seams over renaming or reorganizing for its own sake.
4. Every extraction must define:
   - what state the new module owns
   - what state it reads from `NodeGraphUI` or runtime
   - what commands/callbacks it emits
5. Avoid replacing one god object with several tightly coupled helper objects.

## The Big 6

| File | Lines | Core Issue |
|------|------:|------------|
| `src/ui/node_graph_draw.cpp` | 5,459 | All drawing code in one file |
| `src/runtime/main.cpp` | 4,800 | `main()` is 2,653 lines alone |
| `src/runtime/control_server.cpp` | 3,742 | HTTP routing + serialization + validation |
| `src/ui/node_graph_input.cpp` | 3,303 | `on_key()` is 997 lines |
| `src/ui/node_graph.cpp` | 2,361 | Implementation of god class |
| `src/ui/node_graph.h` | 1,097 | ~158 member vars, 28 boolean state flags, 140+ methods |

## Phase 1: Extract Modal and Dialog State

- [ ] Complete

### Goal

Reduce the boolean/modal-state explosion in `NodeGraphUI` before attempting broader controller extraction.

### Scope

Extract a focused `DialogManager` or equivalent modal-state subsystem from:

- [src/ui/node_graph.h](/Users/jeff/Developer/vivid/src/ui/node_graph.h)
- [src/ui/node_graph.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph.cpp)
- [src/ui/node_graph_draw.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph_draw.cpp)
- [src/ui/node_graph_input.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph_input.cpp)

Candidate responsibilities:

- preferences dialog
- package browser
- example browser
- about dialog
- MCP setup dialog
- create/clone/save confirms
- other global modal/popover state that is currently tracked by boolean flags

### Ownership Boundary

The extracted module should own:

- modal open/close state
- modal stacking / active modal tracking
- modal-local transient input state where possible

`NodeGraphUI` should retain:

- graph/canvas state
- selection state
- inspector state
- high-level command routing

### Acceptance Criteria

- `NodeGraphUI::wants_keyboard()` becomes materially smaller and delegates modal state checks.
- At least one coherent family of modal booleans is removed from `NodeGraphUI`.
- Modal drawing and modal input handling go through a shared state owner instead of scattered booleans.
- No visible behavior regression in modal open/close flow.

## Phase 2: Extract Inspector Surface

- [ ] Complete

### Goal

Split the inspector into a dedicated subsystem after modal state is under control.

### Scope

Extract an `InspectorController` or equivalent from:

- [src/ui/node_graph_draw.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph_draw.cpp)
- [src/ui/node_graph_input.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph_input.cpp)
- [src/ui/node_graph.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph.cpp)
- [src/ui/node_graph.h](/Users/jeff/Developer/vivid/src/ui/node_graph.h)

Responsibilities:

- parameter editing
- inspector layout
- custom inspector invocation
- output display
- inspector-local scrolling
- inspector-local editing state

### Ownership Boundary

The inspector module should own:

- inspector-local editing state
- inspector-local layout bookkeeping
- custom-inspector interaction state

It may read:

- current selected node snapshot
- UI style/theme
- command sink callbacks

It should not own:

- global modal/dialog state
- graph panning/zoom
- chooser/session grid state

### Acceptance Criteria

- inspector draw methods are no longer spread across `NodeGraphUI` without a clear boundary
- inspector-related state in `NodeGraphUI` is materially reduced
- custom inspector behavior still works
- no regression in parameter editing, scrolling, or output display

## Phase 3: Split `main.cpp` by Runtime Responsibilities

- [ ] Complete

### Goal

Reduce `main.cpp` by extracting real subsystems, not just moving functions around arbitrarily.

### Scope

Extract named runtime modules from [src/runtime/main.cpp](/Users/jeff/Developer/vivid/src/runtime/main.cpp):

1. window and event bootstrap
2. UI test runner
3. capture/export coordination

Suggested destination files:

- `src/runtime/window_runtime.*`
- `src/runtime/ui_test_runner.*`
- `src/runtime/capture_runtime.*`

Exact filenames can change, but the destination modules should be concrete and purpose-driven.

### Acceptance Criteria

- `main()` becomes substantially smaller and more orchestration-focused
- extracted code has a clearer API boundary than the original inline code
- no change in startup behavior, window lifecycle, or capture behavior

## Phase 4: Introduce Mode-Aware Input Dispatch

- [ ] Complete

### Goal

Refactor the `NodeGraphUI` input path so keyboard handling is organized by UI mode/state before layering a shortcut table on top.

### Scope

Refactor [src/ui/node_graph_input.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph_input.cpp), especially `on_key()`.

Do not start with “command registry” as the only abstraction. First establish mode-aware dispatch such as:

- global graph mode
- chooser mode
- inspector editing mode
- modal/dialog mode
- text entry mode

Once that boundary exists, a command registry or keybinding table may sit inside the global-mode path.

### Acceptance Criteria

- `on_key()` is materially smaller
- mode-specific input paths are separated and easier to reason about
- keyboard behavior is unchanged for existing shortcuts and modal interactions
- shortcut logic becomes more discoverable without hiding state-dependent behavior

## Phase 5: Split `control_server.cpp` by Concern

- [ ] Complete

### Goal

Keep route dispatch thin by extracting stable, testable subsystems from the control server.

### Scope

Split [src/runtime/control_server.cpp](/Users/jeff/Developer/vivid/src/runtime/control_server.cpp) along real concern boundaries, for example:

1. graph serialization / graph inspection responses
2. checks and diagnostics engine
3. package-related route helpers
4. capture-related route helpers

Possible destination files:

- `src/runtime/control_server_graph_serialization.*`
- `src/runtime/control_server_checks.*`
- `src/runtime/control_server_packages.*`
- `src/runtime/control_server_capture.*`

The dispatch file should remain the routing/orchestration layer, not the implementation home for every response builder.

### Acceptance Criteria

- dispatch routing in `control_server.cpp` is visibly thinner
- extracted modules are organized by behavior, not by arbitrary line-count slicing
- existing control-server tests still pass without behavioral changes

## Phase 6: Extract Remaining UI Controllers Only After State Boundaries Are Clear

- [ ] Complete

### Goal

Finish the `NodeGraphUI` decomposition only after modal and inspector ownership boundaries are proven.

### Scope

Potential extractions:

1. `ChooserController`
2. `SessionGridController`
3. `StickyNoteController`

These should only move once their state boundaries are understood. This phase is intentionally later than the original plan to avoid premature controller proliferation.

### Acceptance Criteria

- each extracted controller has a clear state owner
- each extracted controller has a narrow public surface
- `NodeGraphUI` is left as a coordinator, not a second hidden state store

## Phase 7: Thumbnail Boilerplate Reduction

- [ ] Complete

### Goal

Reduce thumbnail implementation boilerplate, but only after the shared thumbnail rendering direction is settled.

### Scope

Do not commit yet to a specific `thumbnail_helpers.h` abstraction until the unified draw/thumbnail API direction is decided.

Once that direction is settled, reduce thumbnail boilerplate using the chosen rendering model.

### Acceptance Criteria

- thumbnail helpers reduce real duplication instead of creating a second mini-framework
- at least one existing thumbnail implementation becomes materially smaller
- mixed GPU/overlay cases remain possible if the runtime supports them

## Verification After Each Phase

Every phase should end with:

- all existing tests pass:
  - `ctest --test-dir build`
- MCP tools still work for basic operations:
  - `inspect_graph`
  - `set_param`
  - related high-value flows for the touched area
- manual smoke test for any touched UI/runtime behavior
- no new compiler warnings

## Notes

This is intentionally not a one-shot rewrite plan.

The purpose is to make the codebase easier to work in without destabilizing a system that is otherwise architecturally sound. The largest risk is replacing one oversized class or file with several poorly-bounded helper types. This plan tries to avoid that by making ownership boundaries explicit before major code movement.
