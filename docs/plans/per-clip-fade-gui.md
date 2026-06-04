# Per-clip fade — GUI affordance

Status: planned · Scope: small · Depends on: launch-fade engine support (shipped)

## Context

Launch fades are implemented in the engine. `queue_scene` / `queue_clip` accept a
`fade_bars` argument, and `RuntimeAPI::apply_clip_params` ramps numeric params from
their current values to the clip's values over that many bars (string params and
bypass switch at the start of the fade), advanced each frame by `tick_param_ramps`.
Crucially, when a launch passes `fade_bars = 0`, `apply_clip_params` falls back to
the clip's own `SessionClipDef::transition_override`:

```cpp
float eff_fade = fade_bars;
if (eff_fade <= 0.0f && clip.transition_override && clip.transition_override->fade)
    eff_fade = clip.transition_override->duration_bars;
```

So **fade is a per-clip property** — a pad swell can fade slow while a drum clip
cuts. That matches how a musician thinks about it; a global "fade mode" toggle does
not (it would apply the wrong value to the wrong clips).

The gap: there is no way to *set* a clip's fade from the GUI. The session grid's
launch path (`NodeGraphUI::session_queue_clip/scene`) passes no `fade_bars`, so every
grid launch is an instant cut — even though the engine would fade automatically if
the clip carried a `transition_override`.

## Plan

### 1. Backend — `set_clip_fade`
Add `RuntimeAPI::set_clip_fade(track_id, clip_id, float fade_bars)`:
- `fade_bars > 0` → `clip->transition_override = {fade: true, duration_bars: fade_bars}`
- `fade_bars <= 0` → `clip->transition_override.reset()` (clears → cut)
- `mark_graph_dirty()`

`SessionClipDef::transition_override` is already serialized round-trip in `graph.cpp`
(clip load ~`:460`, save ~`:1861`), so no serialization work is needed. Add a thin
`Graph::set_clip_fade` (find_clip → set/clear the optional) or set it directly in
the RuntimeAPI method via `graph_.find_clip(...)`.

### 2. Control server + MCP
- `control_server_dispatch.cpp`: route `set_clip_fade` (`track_id`, `clip_id`,
  `fade_bars`).
- `mcp/vivid_mcp.py`: `set_clip_fade(track_id, clip_id, fade_bars)` tool.
- `inspect_clip` should report the transition (extend its JSON with
  `{"fade_bars": ...}` when set — `control_server_query.cpp` `handle_inspect_clip`).

### 3. Snapshot
`SessionClipSnap.has_fade` / `fade_bars` already exist (populated by the snapshot
builder from `clip.transition_override`). No change.

### 4. GUI affordance (per-clip)
- Add a **"Set Fade ▸ Off / 1 / 2 / 4 bar"** entry to the clip-cell right-click
  menu. The menu labels live in `node_graph_draw_elements.cpp:1314`
  (`clip_cell_labels`); the action handler is `node_graph_input_click.cpp:233`
  (`session_ctx_menu_idx_ == 3`). Either a flyout submenu or a small inline cycle.
- Wire a new `UICommandSink::session_set_clip_fade(track, clip, fade_bars)` (default
  no-op) + `RuntimeCommandSink` impl calling `api_.set_clip_fade(...)`
  (`runtime_command_sink.h`), undo-tracked like the other clip edits.
- Optionally surface the current fade in the clip inspector header
  (`draw_clip_inspector` already shows `has_fade`/`fade_bars`).

### 5. No engine change
Once a clip has a `transition_override`, a normal grid launch
(`session_queue_clip`, `fade_bars = 0`) fades automatically via the fallback above.
The grid path does not need to learn about fades.

## Files
- `src/runtime/control/runtime_api.{h,_session.cpp}` — `set_clip_fade`
- `src/runtime/graph/graph.{h,cpp}` — optional `set_clip_fade` helper
- `src/runtime/control/control_server_dispatch.cpp` — route
- `src/runtime/control/control_server_query.cpp` — `inspect_clip` fade field
- `mcp/vivid_mcp.py` — `set_clip_fade` tool
- `src/ui/ui_command_sink.h`, `src/runtime/control/runtime_command_sink.h` — sink hop
- `src/ui/graph/node_graph_draw_elements.cpp`, `node_graph_input_click.cpp` — menu + action

## Verification
- `set_clip_fade(track, clip, 2)` → `inspect_clip` shows `fade_bars: 2`.
- Launch that clip from the grid (instant quantize) → the clip's numeric params
  glide over 2 bars instead of cutting (sample a param via `get_param`, as in the
  launch-fade verification: a smooth ramp, not a jump).
- `set_clip_fade(track, clip, 0)` → transition cleared, launch cuts again.
- Right-click a clip cell in the GUI → "Set Fade ▸ 2 bar" → same behavior.
