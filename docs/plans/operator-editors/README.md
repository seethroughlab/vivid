# Dedicated Operator Editor Windows

## Summary

Add dedicated editor windows for complex operators such as DrumSequencer, MSEG, Tracker, and future sampler or curve editors. The editor mechanism must be reusable by any operator package, must fit the current Vivid ABI and hot-reload model, and must not rely on host-side special cases for individual operators.

This directory is the implementation plan for that work. It is intentionally explicit about repo paths, lifecycle boundaries, and non-goals so it can be implemented without rediscovering product decisions during coding.

## Locked Decisions

These decisions are fixed for v1 and should not be reopened during implementation unless the plan docs are revised first.

1. **No ABI version bump.** Editor support is added as optional exports under the current operator ABI model. `VIVID_OPERATOR_ABI_VERSION` remains the stale-build detector described in `docs/ARCHITECTURE.md`, not a compatibility matrix.
2. **Preserve current node double-click behavior.** Double-click continues to open existing source or clone flows. Editor opening is exposed through an inspector button and an official `Cmd+E` / `Ctrl+E` shortcut.
3. **Route editor mutations through `UICommandSink` / `RuntimeCommandSink`.** Do not route editor-originated param writes straight to `RuntimeAPI`; the existing sink path preserves undo capture and command coalescing.
4. **Close editor windows on runtime reload boundaries.** Any runtime rebuild or apply that swaps compiled graph state is treated as a hard lifecycle boundary. Use `RuntimeAPI::reload_serial()` to detect that boundary and close all editor windows.
5. **Name only real files.** This plan uses the current repo layout. Do not introduce docs references to nonexistent paths such as per-operator `CMakeLists.txt`, `src/runtime/core/CMakeLists.txt`, or `drum_sequencer.h`.

## Why This Fits The Current Architecture

This feature is additive to systems that already exist in the repo:

- `src/runtime/debug/output_window.{h,cpp}` already proves that Vivid can host a second GLFW window with its own `WGPUSurface` while sharing the main device and queue.
- `src/ui/rendering/renderer_2d.{h,cpp}` already exposes the drawing surface used by custom operator inspectors and can be reused in a second window.
- `src/ui/graph/node_graph_draw_inspector_sections.cpp` already adapts host UI state into `VividInspectorContext`, including command routing through `UICommandSink`.
- `src/runtime/control/runtime_command_sink.h` already wraps parameter writes with undo snapshots and coalescing.
- `src/runtime/control/runtime_api.h` already exposes `reload_serial()`, and `src/runtime/core/main.cpp` already reacts to reload boundaries centrally.

The implementation is therefore a thin ABI extension, a window manager, host affordances, and a first adopter. It is not a UI rewrite.

## Cross-Cutting Files

| Concern | Real Path |
|---|---|
| ABI macros | `src/operator_api/operator.h` |
| ABI types | `src/operator_api/types.h` |
| Optional symbol loading | `src/runtime/operators/operator_loader.{h,cpp}` |
| Operator metadata cache | `src/runtime/operators/operator_info_cache.h` |
| UI-visible operator metadata | `src/ui/graph/graph_snapshot.h` |
| Secondary-window reference | `src/runtime/debug/output_window.{h,cpp}` |
| Main-window input callbacks | `src/runtime/core/window_manager.{h,cpp}` |
| Main loop and reload boundary | `src/runtime/core/main.cpp` |
| Inspector drawing | `src/ui/graph/node_graph_draw_inspector.cpp` |
| Inspector custom context bridge | `src/ui/graph/node_graph_draw_inspector_sections.cpp` |
| Node double-click behavior | `src/ui/graph/node_graph_input_click.cpp` |
| Settings persistence | `src/runtime/core/settings.{h,cpp}` |
| DrumSequencer implementation | `operators/control/drum_sequencer/drum_sequencer.cpp` |
| DrumSequencer shared state | `operators/control/drum_sequencer/drum_sequencer_core.h` |
| DrumSequencer inspector UI | `operators/control/drum_sequencer/drum_sequencer_inspector.cpp` |
| Operator build wiring | `cmake/operators.cmake` |

## Phase Index

- [Phase 1: Editor ABI](phase-1-abi.md)
- [Phase 2: Editor Window Manager](phase-2-window-manager.md)
- [Phase 3: Host Integration](phase-3-host-integration.md)
- [Phase 4: First Adopter - DrumSequencer](phase-4-first-adopter.md)

Phases 1 through 3 are sequential. Phase 4 depends on all three and validates the full path end to end.

## Scope Boundaries

### In Scope

- One editor window per node instance.
- Separate native OS windows that can be resized and moved independently of the main graph window.
- Editor drawing that reuses `Renderer2D`, `VividDrawAPI`, and the existing command path.
- Per-operator-type geometry persistence.
- Inspector and editor surfaces coexisting for the same operator.

### Out Of Scope For V1

- Docking, panes, or tabbed multi-surface editors.
- Multiple simultaneous windows for the same node instance.
- Changing current node double-click semantics.
- Surviving runtime reloads by rebinding old editor windows to new compiled instances.
- Automated second-window UI scripting unless the test harness is explicitly extended in the same implementation.
- MSEG migration in the same change as the initial DrumSequencer adoption.

## End-To-End Acceptance

The feature is complete when all of the following are true:

1. Selecting an editor-capable node shows an **Open Editor** control in the inspector, and `Cmd+E` / `Ctrl+E` opens that editor.
2. The editor opens as a separate native window, can be resized and moved across monitors, and reuses the same window geometry the next time an editor of that operator type opens.
3. Editing in the editor updates the inspector immediately, and inspector edits update the editor immediately.
4. Deleting the node, loading a different graph, or rebuilding a package closes affected editor windows cleanly.
5. Any runtime reload boundary detected via `reload_serial()` closes all editor windows instead of leaving stale instance pointers live.
6. Operators without editors remain unchanged: no new button, no behavior regression, no ABI break.

## Verification Strategy

Each phase contains its own acceptance criteria, but implementation should keep automated and manual validation separate.

### Automated Coverage

- Loader and metadata-cache tests for the optional editor exports.
- Unit coverage for editor-window bookkeeping and reload-triggered teardown.
- Settings serialization coverage for editor geometry.
- UI-level checks for inspector button visibility and editor-open shortcut dispatch where possible in the existing single-window harness.
- DrumSequencer helper-level interaction tests where they can be exercised without driving a second GLFW window.

### Manual QA

- Open and refocus an editor window from the inspector and from the shortcut.
- Resize and move the window across monitors, then relaunch and verify persistence.
- Confirm live sync between editor and inspector.
- Confirm clean teardown on node delete, graph load, and package rebuild.
- Confirm keyboard focus stays inside the editor when the editor requests keyboard capture.

### Editor-window automation

The UI script runner routes actions to editor windows via the
`target_window` field on every `UITestAction` (mouse/key/char/screenshot)
and a new `open_editor` action type that drives
`EditorWindowManager::open(node_id)`. `--editor-screenshot NODE=PATH` on
the vivid CLI captures the named editor's surface to PNG via
`EditorWindowManager::capture_surface_png`.

Reference smoke cases for DrumSequencer and MSEG live in
`tests/ui/test_ui_screenshot_smoke_cases.inc`
(`drum sequencer editor`, `mseg editor`). They spawn `vivid` with a
fixture graph, run a short scripted interaction, and capture the editor
surface.

Secondary editor windows gate their close on an explicit-close
sentinel so spurious macOS-subprocess close signals don't tear them
down before capture (see
`src/runtime/core/editor_window_manager.cpp` — `close_cb` +
`explicit_close_requested`). With that in place the two editor smoke
cases pass end-to-end under `VIVID_UI_SMOKE_LANE=gui_smoke` with
baseline fingerprint diffs.
