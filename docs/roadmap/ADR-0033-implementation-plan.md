# ADR-0033 Implementation Plan — Composition Editing Ergonomics

Tracks [ADR-0033](../decisions/ADR-0033-composition-editing-ergonomics.md). Extends ADR-0017
(reversible edits), ADR-0022 (session audio graph), ADR-0023 (shared graph UI substrate).

Status: **Phase 1 code-complete** (2026-08-04) — builds clean, full ctest 80/80 green, app launches +
renders. Interactive gesture confirmation (marquee/toggle/group-drag by mouse) + commit pending.

## Grounding: what already exists

| Subsystem | State | Consequence |
|---|---|---|
| **EditGateway** (`app/src/app/edit_gateway.*`) | Snapshot-based undo. Mutate model → `note_edit(label)` or `begin_group`/`end_group`. MCP edits auto-captured via `edit_methods.cpp` table. | No inverse-ops. New edit = mutate → note → serialize in `persist.cpp` → register MCP method. |
| **Graph UI substrate** (ADR-0023) | `node_view.h` (camera), `node_canvas.h` (marks), `graph_canvas.h` (shared card loop). **Layer 3 (interaction controller) deliberately deferred.** | Selection/hit-test/drag are per-editor. Multi-select is the deferred Layer-3 work. |
| **Selection** | Single-`int`: `sel_op_` (`node_graph.h:239`), `sel_node_` + `Window::sel_audio_node` (audio). | Replace with shared selection set; keep `primary()` accessor. |
| **ClipEditor** (`ui/clip_editor.*`) | Full marquee + multi-select mask + copy/paste/duplicate for MIDI notes. | In-repo reference FSM (not on graph substrate). |
| **Audio model** (ADR-0022) | Pure core `audio_graph.h`; host binding `vst3_host_internal.h` (`GNKind`, `GNodeBind`, `Session::next_gnid`). Edge kinds Audio/Note/Control + cross-track `X*Edge`. | Node classification via `is_source`/ports, `audio_role`, `GNKind`. |
| **Track solo/mute** | Exists — `recompute_mix_scales`, `mix_scale` atomic, MCP `set_track_mute`/`set_track_solo`. | Node solo reuses effective-gain pattern. |
| **Bypass** | Does not exist at any level. | Greenfield; `remove_node_bridged` gives effect-heal semantics. |
| **Sticky notes / node rename** | Neither exists. Only `set_scene_name`. | New persisted entity + `set_node_name` mirroring `set_scene_name`. |
| **MCP** | Python `vivid_mcp.py` → HTTP → C++ `control_handlers_*.cpp`; parity guard enforces 1:1 names. | Add-a-tool = handler + `edit_methods.cpp` row + Python tool. |

**Classic port source:** `docs/roadmap/classic-platform-gap.md:73-79` — the Classic build already has
`selected_node_ids_`, marquee, group drag, `copy_selected_nodes`/`paste_copied_nodes`, `B`/`S`
bypass/solo (BFS upstream), sticky notes. Port-as-reference (trunk substrate diverged by deferring
Layer 3), mine for marquee math (Phase 1) and bypass BFS (Phases 3/4).

## Phases (each = one shippable PR)

### Phase 1 — Multi-select foundation *(Decision 1)* — IN PROGRESS
Shared `GraphSelection` (`std::set<int>` ids + anchor + marquee rect + additive/toggle) as
"Layer-3-lite". Per-editor hit-test stays (entangled with port geometry per ADR-0023); each editor
feeds the shared controller. Marquee rect→node intersection shared given editor node-rects.
Replaces `sel_op_`/`sel_node_`/`Window::sel_audio_node` (keep `primary()`). Group-drag brackets
`begin_group("Move Nodes")`. Selection is view-state — not undoable/persisted.
Files: new `ui/graph_selection.h`; `ui/node_graph.*`, `ui/audio_node_graph.*`, `app/input_graph.cpp`,
`app/frame.cpp:1019`, `app/input_kbd_edit.cpp`, `node_canvas.h`. Tests: pure marquee-intersect +
additive/toggle logic. Risk: low-medium.

### Phase 2 — Copy / paste / duplicate / delete *(Decision 2)*
Shared command shell, per-domain clipboard serialization. New gnids on paste; external edges omitted.
**Hard part:** pasting plugin/sampler nodes = async re-instantiation via authoritative-graph
persist+rebind. MCP `duplicate_nodes`/`delete_nodes`. Risk: high (plugin duplication + async rebind).

### Phase 3 — Bypass *(Decision 3)*
Greenfield `bool bypassed` with kind-aware routing: effects pass-through (`remove_node_bridged` heal),
sources gate-to-silence (scene-gating pattern), modulators freeze. RT-published via generation-counter.
Persisted + undoable. MCP `set_node_bypass`. Update audio-engine curated-targets list. Risk: medium-high.

### Phase 4 — Node solo / audition *(Decision 4)*
BFS upstream (port Classic) or branch isolation via transient effective-gain (no edge rewrite;
ADR-0022 "solo is never a node property"). Performance state — not undoable/persisted (like
`launch_clip`). Risk: medium.

### Phase 5 — Sticky notes & per-node labels *(Decision 5)*
New persisted annotation entity (id/text/rect/color) — data-node title persist pattern; schema
v3→v4 + migration. Per-node `set_node_name` mirroring `set_scene_name`. Risk: low (additive).

### Cross-cutting *(Decision 6)*
Every command through EditGateway; saveable; MCP-exposed; canonical persistence tests. Guardrails:
full ctest before push; MCP parity guard green; new AUDIO_ENGINE tests added to curated-targets list.

## Sequencing
1. Phase 1 (keystone). 2. Phase 5 (parallel, low-risk early win). 3. Phase 2 (after 1; highest risk).
4. Phase 3 (after 1). 5. Phase 4 (after 1; trailing).
