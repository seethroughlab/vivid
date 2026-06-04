# Clip inspector — in-panel value editing

Status: planned · Scope: medium · Depends on: read-only clip inspector + `update_clip_param` (shipped)

## Context

A session clip is a stored snapshot: `params {node_id → {param → float}}`,
`string_params`, and `bypass {node_id → bool}` (`SessionClipDef`,
`src/runtime/graph/graph.h`). The GUI already has a **read-only** clip inspector —
right-click a clip cell → "Open Clip" selects it and `NodeGraphUI::draw_clip_inspector`
(`node_graph_draw_inspector.cpp`) renders its contents as text. The edit command
layer exists too: `update_clip_param` / `update_clip_string_param` /
`update_clip_bypass` (RuntimeAPI + control server + MCP), surfaced to the UI as
`UICommandSink::session_update_clip_param` / `session_update_clip_bypass`.

What's missing is **in-panel editing**: dragging a slider / toggling a bool / picking
an enum in the clip inspector to change a stored value. The constraint: **reuse the
existing inspector widgets** — do not re-implement sliders/knobs/toggles/enums/file
pickers for clips.

## Key finding — the widgets are reusable via a value source/sink indirection

The node inspector's widget system is already structured so that the *only* things
tying it to a live node are the value it reads and the command it writes — the
rendering and metadata are reusable as-is.

- **Render + read:** `draw_one_inspector_param()` (`node_graph_draw_inspector_params.cpp:1199`)
  pulls the current value internally at ~`:1208` (`float val = node.param_values[pi];`,
  and `node.file_param_values` for strings). All widget *metadata* — min/max, choices,
  type, display hint — comes from the operator descriptor via
  `node.find_param(name)` → `op_info->params[...]` (`graph_snapshot.h:268`). A clip
  stores only values, so **the owning node's live descriptor stays the source of
  truth for how to render** (always available; the owning node exists in the
  snapshot via `track.owned_node_ids`).
- **Write:** edit dispatch is centralized to ~6 `commands_.set_param` /
  `set_string_param` sites: slider drag (`node_graph_update_drag.cpp:~173`), bool
  toggle / dropdown / color / file picker (`node_graph_input_click_widgets.cpp`
  ~`:965` / ~`:183` / ~`:233` / ~`:989`).
- **Hit-testing:** each widget rect (`InspectorController::slider_rects`,
  `bool_rects`, `dropdown_rects`, …) already carries `node_id` + `param_name`, so the
  identity needed to target a clip edit is already on the rect.

So the work is **not** new widgets — it's threading a small context that swaps the
read source and the write target.

## Recommended approach — `ClipEditContext`

1. **Context member** on `NodeGraphUI`:
   ```cpp
   struct ClipEditContext {
       bool active = false;
       std::string track_id, clip_id;
       const SessionClipSnap* clip = nullptr;   // from the snapshot
   };
   ClipEditContext clip_edit_;
   ```
   Set it when a clip is open (we already track `selected_clip_track_/id_`); resolve
   `clip` from `snap_.session.find_track(track)->clips`. Clear it when a node is
   selected (mirrors the existing clip/node mutual-exclusion in `draw_inspector`).

2. **Reuse the node param path in `draw_clip_inspector`:** instead of the read-only
   text list, iterate the clip's owning nodes (`track.owned_node_ids`), look up each
   one's live `NodeSnapshot`, and render its params through the *existing*
   `draw_inspector_params` / `draw_one_inspector_param` + `InspectorLayout` machinery,
   with `clip_edit_.active = true`. Render the node's **full** param set (see decision
   below) so unstored params can be added.

3. **Value READ indirection** (one site, ~`:1208`):
   ```cpp
   float val;
   if (clip_edit_.active && clip_edit_.clip) {
       const auto* m = find_node_map(clip_edit_.clip->params, node.node_id);
       val = (m && m->count(pd.name)) ? (*m)[pd.name] : pd.default_value;
   } else {
       val = node.param_values[pi];
   }
   ```
   String params read from `clip->string_params`; bypass reads from `clip->bypass`.

4. **Value WRITE indirection** (the ~6 dispatch sites): when `clip_edit_.active`,
   route to the clip commands instead of the live ones:
   ```cpp
   if (clip_edit_.active)
       commands_.session_update_clip_param(clip_edit_.track_id, clip_edit_.clip_id,
                                            node_id, param, val);
   else
       commands_.set_param(node_id, param, val);
   ```
   Bool → `session_update_clip_bypass` (for a node's bypass) or
   `session_update_clip_param` (for a bool *param*); string/file →
   `session_update_clip_string_param`. Edits **upsert** into the clip (adding a value
   the original capture didn't include).

5. **Fill the one missing sink hop:** add
   `UICommandSink::session_update_clip_string_param` (+ `RuntimeCommandSink` impl) —
   the RuntimeAPI/control/MCP `update_clip_string_param` already exists, only the UI
   sink method is missing.

**Unchanged:** every widget renderer, all hit-testing (`InspectorController` rect
arrays), and all drag handling. Estimated ~50–80 lines, almost entirely the read/
write conditionals plus the context plumbing.

## Decisions to make (call out in implementation)

- **Show stored-only vs all node params.** Recommend rendering the node's *full*
  param set (reading the stored value when present, else the node default) so the
  inspector is a true editor and you can add a param to the clip by tweaking it.
  Trade-off: a clip then "covers" more params than it was captured with — which is
  the intended power, but worth a subtle visual cue (e.g. dim un-stored params until
  touched).
- **Live vs stored is not the same thing.** Editing a clip changes the *saved
  snapshot*, not the live node — the live node only changes when the clip is
  launched. The panel should make this explicit (a small "editing saved clip — launch
  to hear" banner) to avoid the "I moved the slider and nothing happened" confusion.
- **Header actions (future, out of scope):** a "Launch" button and an "Update from
  live" button in the clip inspector header are natural next steps once editing
  lands.

## Files
- `src/ui/graph/node_graph.h` — `ClipEditContext`
- `src/ui/graph/node_graph_draw_inspector.cpp` — `draw_clip_inspector` reuses the param path
- `src/ui/graph/node_graph_draw_inspector_params.cpp` — value-read indirection (~`:1208`)
- `src/ui/graph/node_graph_update_drag.cpp` — slider write indirection (~`:173`)
- `src/ui/graph/node_graph_input_click_widgets.cpp` — bool/dropdown/color/file write indirection
- `src/ui/ui_command_sink.h`, `src/runtime/control/runtime_command_sink.h` — `session_update_clip_string_param`

## Verification
- Open a clip; drag a slider → `inspect_clip` shows the new stored value; the live
  node param is unchanged (`get_param`/`inspect_node`) until the clip is launched.
- Toggle a bool / pick an enum / change a node's bypass → all reflected by
  `inspect_clip` (params / bypass).
- Launch the edited clip → the live node now takes the edited values (with fade if
  the clip has a transition — see `per-clip-fade-gui.md`).
- Regression: the normal node inspector still edits live nodes when no clip is open.
