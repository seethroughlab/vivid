# Q1 Plan: Session / Variation Exploration Surface

## Summary

Turn the existing bottom variation strip into a true second exploration surface built around visible branching, quick auditioning, and low-friction promotion of alternate states.

This plan keeps the current architectural decisions intact:

- the node graph remains the structural and connection editor
- variations remain JSON-backed graph-state snapshots
- no patchbay, no connection matrix, no REPL in this phase

The product target for Q1 is:

- users can see multiple alternate states at once
- branching is more spatial and less sequential
- saving, duplicating, recalling, comparing, and promoting variations feels like one coherent workflow
- the scorecard can treat the session/variation surface as a real first-class experimentation interface

## Implementation Changes

### 1. Promote the current variation strip into a session surface

Build on the existing variation model in `Graph`, `RuntimeAPI`, `ControlServer`, `GraphSnapshot`, and `NodeGraphUI`, rather than inventing a new storage layer.

Implement the session surface as an expanded retained-mode panel with these concepts:

- **Variation cards/cells** instead of a minimal strip
- one **working state** concept
- one **active variation**
- one **queued variation** for quantized switching
- visible **dirty state** when the live graph has diverged from the active variation
- explicit **branch** and **promote/update** actions

The session surface should stay in the existing bottom/workspace-adjacent role described in `docs/INTERFACE.md`, but gain enough height and structure to support exploration rather than only recall.

### 2. Lock the session interaction model

Use this exact workflow model:

- **Save New**
  - captures the current live graph state into a new variation
  - default name is generated, then editable inline
- **Branch**
  - duplicates the currently active variation into a new sibling variation
  - if no active variation exists, branch behaves like `Save New`
- **Recall**
  - immediately applies a variation when quantization is `instant`
  - otherwise arms the variation and shows it as queued
- **Update / Promote**
  - writes the current live state back into the active variation
  - only enabled when `variation_dirty = true`
- **Rename**
  - inline rename on double-click or explicit action
- **Delete**
  - removes a variation with a lightweight confirmation affordance
- **Duplicate**
  - duplicates a variation without recalling it
- **Reorder**
  - drag reorder in the session surface
  - order is meaningful and persisted
- **Compare/Audition**
  - single-click previews/selects a card
  - fast toggling between two variations must be supported without leaving the surface

Defaults:
- cards are arranged in one horizontal lane first
- horizontal scrolling is acceptable for v1
- no nested folders, tags, or multi-row grouping in Q1
- no crossfade editor in Q1 beyond the existing quantized switch behavior

### 3. Expand the data model only where needed

Keep `Graph` variations as the source of truth, but extend the model minimally so the UI can support the new session behavior without ad hoc state.

Needed changes:

- persist **variation order** as the array order already implies
- add an explicit **duplicate variation** command in `RuntimeAPI` and control-server
- add **reorder variation** support in `RuntimeAPI` and control-server
- keep `active_variation`, `queued_variation`, `variation_dirty`, and `quantize_clock_node`
- do not add tags, folders, or grouping metadata in Q1

Public/control interfaces to add:

- `duplicate_variation(name, new_name)`
- `move_variation(name, to_index)`

These should be exposed consistently through:
- `RuntimeAPI`
- control-server endpoints
- `UICommandSink` / runtime command sink
- graph snapshot fields if needed for the UI

### 4. Define the UI states and visuals explicitly

The session surface should visually distinguish:

- **active**
- **queued**
- **dirty**
- **selected/focused**
- **default/inactive**

Recommended card contents:

- variation name
- active/queued markers
- dirty marker when live graph has diverged
- one compact metadata line:
  - node/param count is optional
  - quantized status is optional
- optional tiny preview swatch/icon only if already cheap to compute
  - do not add heavyweight thumbnail generation for Q1

Required affordances:

- inline rename
- duplicate
- delete
- save new
- update/promote
- drag reorder
- click to recall/select
- optional context menu if it simplifies density

The session surface should remain content-forward and match the current dark-steel UI language. It should feel like an exploration workspace, not a transport widget.

### 5. Keep the runtime contract simple and trustworthy

Variation actions must preserve the already-hardened runtime guarantees:

- recalling a variation updates the live graph deterministically
- update/promote does not lose unrelated graph state
- queued variation switching remains truthful in the UI
- branch/duplicate/reorder are stable under save/load
- no hidden mutation outside the graph/variation model

Do not add background mutation logic or speculative preview state in Q1.

### 6. Document the creator workflow

Update docs so the session surface becomes part of the visible product story, not just an internal feature:

- `docs/INTERFACE.md`
- `docs/GETTING-STARTED.md`
- optionally one short demo-oriented note in `docs/PRD.md` only if wording needs to reflect the shipped shape

Document one canonical workflow:

1. build a patch in the graph
2. save variation `A`
3. branch to `B`
4. audition between them
5. update one variation after tweaking
6. queue a quantized switch
7. keep exploring without rewiring the graph

## Test Plan

### Runtime / graph tests
Add or extend tests for:

- duplicate variation creates an exact copy with a new name
- reorder variation persists through save/load
- active variation and queued variation remain coherent after reorder/delete
- branch from active variation preserves state exactly
- branch with no active variation behaves like save-new
- update/promote clears dirty state correctly
- dirty state becomes true after mutation away from active variation
- quantized queued recall still works after reorder

### Control-server tests
Extend control-server coverage for:

- `duplicate_variation`
- `move_variation`
- session actions reflected in `inspect_graph` / graph snapshot responses
- queued/active/dirty state remaining truthful after branch/reorder/update flows

### UI tests
Extend UI coverage for:

- session surface card hit-testing
- inline rename flow
- duplicate/delete/reorder actions
- dirty marker visibility
- queued/active card state rendering
- branch/update action enablement rules

### Acceptance scenarios
Q1 is complete when these user-level workflows all work:

1. Save a variation, tweak the patch, and visibly see that the live state is dirty.
2. Branch the active variation into a new sibling without losing the original.
3. Toggle between two variations quickly from the session surface.
4. Reorder variations and keep that order after save/load.
5. Queue a variation change with quantization and see the queued state clearly before it activates.
6. Promote the current live state back into the active variation without leaving the session surface.

## Assumptions And Defaults

- The existing variation snapshot model is kept.
- The node graph remains the only wiring surface.
- Q1 focuses on one strong session/variation surface, not multiple new experimentation interfaces.
- No REPL, parameter-space explorer, state-machine UI, or pattern-algebra UI is included in this phase.
- Horizontal session cards with drag reorder are the default first implementation.
- The minimum new public actions are `duplicate_variation` and `move_variation`; everything else should reuse the current variation command family.
