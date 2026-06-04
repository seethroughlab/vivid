# Plan — Deterministic, grouped undo

*(Formerly "editor undo atomic groups.")*

## Context

Editor- and inspector-window edits route `set_param` → `RuntimeCommandSink::set_param` → `capture_undo_snapshot("param:<node>/<param>")` (`src/runtime/control/runtime_command_sink.h:27–30`). Coalescing is a **300 ms wall-clock timer keyed on the param** (`runtime_command_sink.cpp:344–361`): a snapshot replaces the top entry only if the same key recurs within 300 ms. Two consequences:

- Bulk actions across *distinct* params don't merge — "Clear pattern" (16 step params), "Randomize", a tracker row swap, an MSEG bulk edit each produce N undo entries and take N Cmd+Z presses.
- Even a single-slider drag relies on a wall-clock window, so a frame hitch can wrongly split or merge a gesture.

Correct outcome: **explicit grouping is the canonical mechanism** — bulk actions *and* continuous gestures bracket a single deterministic undo entry; the 300 ms timer is demoted to a fallback for callers that can't bracket (external/MCP rapid sets).

We are free to break ABI / drop backwards compatibility.

## Scope decision (deliberately bounded)

Keep the **snapshot-based** undo model: `UndoManager` (`src/runtime/core/undo_manager.h`) stores full-graph JSON snapshots via `push(json, replaceTop)`. Converting to command/delta undo would be more scalable but is a separate, much larger rearchitecture — explicitly **not** part of this plan (recorded here as a known limitation, not smuggled in). Grouping is correct and complete *within* the snapshot model: one capture at group-end records the post-action graph state.

## Current state (verified)
- `VividInspectorCommandAPI` (`src/operator_api/types.h:507–511`) exposes only `set_param` / `set_string_param`.
- Editor path: `src/runtime/core/editor_window_manager.cpp` builds `EdCmdCtx{sink, node_id}`; thunks `ed_set_param`/`ed_set_string_param` (~42–51) call `ctx->sink->set_param(...)`; wired at ~746–748.
- Inspector sidebar fills an equivalent command API in `src/ui/graph/node_graph_input_click_widgets.cpp` / `node_graph_draw_inspector_sections.cpp`.
- `UICommandSink` (`src/ui/ui_command_sink.h:171–175`) has undo/redo virtuals, no grouping.
- `RuntimeCommandSink` holds `UndoManager undo_manager_`, `last_coalesce_key_`, `last_coalesce_time_` (`runtime_command_sink.h:456–458`); `capture_undo_snapshot` is private (line 434).

## Approach

### Step 1 — Operator API (ABI bump, no guards)
Append to `VividInspectorCommandAPI` (`types.h:507–511`):
```c
void (*begin_undo_group)(void*, const char* label);
void (*end_undo_group)(void*);
```
Bump `VIVID_OPERATOR_ABI_VERSION`; rebuild all operators.

### Step 2 — `UICommandSink` interface
Add to `src/ui/ui_command_sink.h` (near the undo block ~171–175):
```cpp
virtual void begin_undo_group(const std::string& label) {}
virtual void end_undo_group() {}
```

### Step 3 — `RuntimeCommandSink` implementation
Add private `int undo_group_depth_ = 0`.
- `begin_undo_group(label)`: on 0→1 transition, clear `last_coalesce_key_` (so the open boundary doesn't time-coalesce with a prior edit). Nesting via the counter.
- `end_undo_group()`: on 1→0, call `capture_undo_snapshot()` once (empty key → distinct entry).
- `capture_undo_snapshot()`: early-return without pushing while `undo_group_depth_ > 0`.
- Safety: any topology mutation or `undo()/redo()` while a group is open force-closes it (snapshot + reset depth to 0).

### Step 4 — Make grouping canonical in the UI (the correctness upgrade)
- **Continuous gestures:** in `node_graph_input_click_widgets.cpp` / `node_graph_draw_inspector_sections.cpp` and `editor_window_manager.cpp`, bracket slider/drag interactions with `begin_undo_group` on drag-start and `end_undo_group` on release → one deterministic entry per gesture, independent of frame timing.
- **Bulk operator actions:** add `ed_begin_undo_group`/`ed_end_undo_group` thunks next to `editor_window_manager.cpp:747–748`, and mirror in the inspector command-API constructor, so operators can bracket their own multi-param actions.
- **Demote the timer:** once UI gestures bracket explicitly, the 300 ms path is a fallback only for non-bracketing callers (external/MCP rapid sets). Keep it for those; it no longer decides UI undo granularity.

### Step 5 — Reference adoption
Bracket DrumSequencer "Clear pattern" / "Randomize" in `operators/control/drum_sequencer/drum_sequencer_editor.cpp` as the canonical example for other operators.

## Files
`src/operator_api/types.h` (+ABI), `src/ui/ui_command_sink.h`, `src/runtime/control/runtime_command_sink.{h,cpp}`, `src/ui/graph/node_graph_input_click_widgets.cpp` (+ inspector sections), `src/runtime/core/editor_window_manager.cpp`, `operators/control/drum_sequencer/drum_sequencer_editor.cpp`.

## Verification
1. Build core + drum_sequencer (background).
2. "Clear pattern" → one Cmd+Z restores the whole pattern; redo re-clears.
3. Drag a slider across several frames (induce a hitch) → exactly one undo entry every time.
4. Single discrete edits still undo individually.
5. Unit test: open a synthetic group, fire a topology change → undo stack stays consistent (force-close works).
