# Dedicated Operator Editor Windows

## Summary

Add a mechanism for complex operators (DrumSequencer, MSEG, Tracker, future sampler/curve/mod-matrix editors) to open their own dedicated editor — preferably in a separate OS window — analogous to Unreal's mesh/material editors or Unity's animation editors. The mechanism must be a stable ABI so any package can declare an editor, not a host-side special case.

## Problem

Today every operator draws into the single right-sidebar inspector (~350 px wide). This is fine for a dozen sliders but hostile for content-heavy operators:

- **DrumSequencer** crams 6 drums × 16 steps × (trigger + two mod lanes) ≈ 96+ editable cells into that strip.
- **MSEG** has 53 hidden params (time/value/curve triples) draggable in a tiny canvas.
- **Tracker** needs a multi-channel × multi-row pattern grid.

We want these authoring surfaces to breathe — resizable, movable to a second monitor — without forcing every operator to build its own host coupling.

## Why this is tractable

Exploration of the codebase surfaced two enabling pieces of existing infrastructure:

1. **Multi-window already works.** `src/runtime/debug/output_window.cpp` creates a second GLFW window with its own `WGPUSurface`, sharing device/queue with the primary window. The editor windows follow this pattern.
2. **Operator-defined UI already works.** `VIVID_INSPECTOR` + `VividInspectorContext` + `VividDrawAPI` (ABI v1) is used by ~8 operators today. A dedicated editor is a superset of that pattern with a bigger canvas and a fuller input event queue.

The plan is therefore a thin ABI extension plus a new window manager, not a ground-up UI rewrite.

## Phase Index

- [Phase 1: Editor ABI](phase-1-abi.md) — `VIVID_EDITOR` macro, `VividEditorContext`, ABI v2, loader plumbing.
- [Phase 2: Editor Window Manager](phase-2-window-manager.md) — secondary GLFW/WGPU windows, per-window input routing, main-loop tick.
- [Phase 3: Host Integration](phase-3-host-integration.md) — "Open Editor" button, double-click to open, settings persistence, lifecycle.
- [Phase 4: First Adopter — DrumSequencer](phase-4-first-adopter.md) — migrate DrumSequencer to prove the feature end-to-end; MSEG is the second candidate.

Phases 1 → 2 → 3 are sequential. Phase 4 depends on all three and validates them.

## Scope Boundaries

**In scope**

- One editor window per node instance (a given DrumSequencer node = at most one editor window at a time).
- Separate OS windows that can be resized, moved across monitors, and whose size/position persists per operator type.
- Inspector drawing stays as today; editor is additive — an operator can offer both.

**Out of scope for v1**

- Docking / splitter panels inside an editor window.
- Multiple simultaneous editors for the same node.
- Live graph-output preview inside the editor window.
- Editors for non-operator concepts (presets, variations, graph-level state) — can be layered on later using the same infrastructure.

## Critical Files (cross-cutting)

| Concern | Path |
|---|---|
| ABI macros / types | `src/operator_api/operator.h`, `src/operator_api/types.h` |
| Loader plumbing | `src/runtime/operators/operator_loader.{h,cpp}` |
| Multi-window reference (read-only) | `src/runtime/debug/output_window.{h,cpp}` |
| New window manager | `src/runtime/core/editor_window_manager.{h,cpp}` (new) |
| Main-loop hook | `src/runtime/core/main.cpp` |
| Per-window input state | `src/runtime/core/window_manager.{h,cpp}` |
| Inspector "Open Editor" button | `src/ui/inspector/inspector_controller.{h,cpp}`, `src/ui/graph/node_graph_draw_inspector_sections.cpp` |
| Settings persistence | `src/runtime/core/settings.h` |
| First adopter | `operators/control/drum_sequencer/*` |

## Reused Infrastructure (don't reinvent)

- `Renderer2D` (`src/ui/rendering/renderer_2d.{h,cpp}`) — instantiate one per editor window; it is not window-bound.
- `populate_draw_api()` (same file) — fills `VividDrawAPI` for the new editor context identically.
- `VividInspectorCommandAPI` — reuse for `set_param` / `set_string_param` from the editor.
- `draw_ui_helpers.h` (`src/operator_api/`) — buttons/tabs/panels work unchanged in the editor surface.
- `OutputWindow` lifecycle pattern — secondary-window creation, surface attach, shared device.

## Overall Verification

Full acceptance is phase-scoped (see each phase's file), but end-to-end the feature is "done" when:

1. Clicking **Open Editor** on a DrumSequencer node opens a separate, resizable window.
2. Edits in the editor immediately reflect in the inspector and vice-versa.
3. Deleting the node, closing the window, unloading the graph, or reloading the dylib all clean up correctly.
4. Editor window size/position persists per operator type across sessions.
5. Older v1 operators continue to work unchanged — no "Open Editor" button appears for them, nothing else regresses.
6. `ctest` (run in background) passes, including a new test that a scaffolded operator with `VIVID_EDITOR` exports the expected symbols.
