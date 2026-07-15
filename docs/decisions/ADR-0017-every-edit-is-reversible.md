# ADR-0017: Every Edit Is Reversible

Status: accepted — undo/redo implemented (see "As built" below). Multi-select/copy/paste split to a
follow-up.

Date: 2026-07-14

Amends: [ADR-0013](ADR-0013-focus-first-strict-zone-ui.md) (the focus-first UI) by adding the one
affordance it assumes but never specifies: the ability to take an action back.

Decided: the application gains **one undo stack**, shared by both graphs and the session,
**snapshot-based** on the existing session serializer. Grouping is **explicit and deterministic**
(`begin_undo_group` / `end_undo_group`), not a wall-clock timer. And node selection stops being a
single `int` and becomes a **set** — which is what unlocks marquee, group-drag, copy, paste, and
duplicate on both canvases at once.

> ### As built (2026-07-14, `feature/undo-redo`, 5 commits G1–G4)
>
> Undo/redo shipped; the mechanism was chosen during implementation to be the *architecturally right*
> one rather than the literal sketch above:
> - **A command sink (`EditGateway`), not per-site `mutate()` hooks.** Every document edit — MCP and
>   UI — routes through one gateway that captures a labeled snapshot. MCP is captured by a table at the
>   dispatch chokepoint (`cli/edit_methods`); UI by gesture bracketing + `note_edit` at each site.
>   The trunk had no single command path, so this *is* the model vivid-classic's `RuntimeCommandSink`
>   proved. The per-site alternative was rejected: a missed site among ~75 handlers is a silent bug.
> - **A completeness audit** (`VIVID_UNDO_AUDIT`) turns any un-routed edit into a failing assertion —
>   the de-risk for the sink's "did the rerouting miss a site?" question. It caught two real bugs during
>   bring-up (plugin `getState()` churn; pre-layout baseline).
> - **Deferred, end-of-frame capture** (not immediate): the audit showed draw-time settling (node
>   auto-positioning) must be in the snapshot.
> - **Snapshot = a canonical document projection** stripping performance/view state (window, pan/zoom,
>   launched clip) and opaque plugin state; **smart restore** tiers audio as Skip / ParamsOnly / Full so
>   a value undo (e.g. a gain drag) never re-instantiates a plugin (verified 0.065s, no reload).
> - **Surfaces**: Cmd+Z / Cmd+Shift+Z / Cmd+Y, a native Edit menu with live labels, MCP `undo`/`redo`.
>
> **Deferred to a follow-up PR**: multi-select + copy/paste/duplicate (the "selection is a set" half).
> In vivid-classic these were an independent subsystem from undo, and undo shipped without them. The
> remaining verification is the interactive `VIVID_UNDO_AUDIT` click-through of the UI edit sites.

## Context

Today, in the trunk, you cannot undo anything you do to a graph.

Undo exists — but only inside the MIDI clip editor, over its note buffer
(`app/src/ui/clip_editor.h:153`: `std::vector<std::vector<ClipNote>> undo_, redo_;`). Step outside
that editor and every action is permanent: add a node, delete a node, rewire a connection, drag a
param, add or remove a track, connect a mapping. There is no Cmd+Z. There is no Edit menu with an
undo item. There is no `undo` control method.

This is not a missing convenience. It is a missing *invariant*. An environment whose entire premise
is live, exploratory rewiring — ADR-0014's "the graph is home", ADR-0016's "shaders are content you
edit" — is an environment that punishes exploration. Every experiment is a commitment.

The second half is selection. Both graphs select exactly one node:

- `app/src/ui/node_graph.h:147` — `int sel_op_ = -1;`
- `app/src/ui/audio_node_graph.h:99` — `int sel_node_ = -1;`

So there is no marquee, no shift-click, no group drag, no copy, no paste, no duplicate, and no
Delete key binding on either canvas. Building a graph of any size means placing every node by hand,
one at a time, forever.

### Why this is cheaper here than it was in classic

**The serializer already exists and is already correct.** `app/src/persist.cpp` gives us:

```cpp
nlohmann::json session_to_json(session::Session*, ui::NodeGraph&, ...);
bool           session_from_json(const nlohmann::json&, session::Session*, ui::NodeGraph&, ...);
```

These are **in-memory** round-trips (the `save_session` / `load_session` file wrappers sit on top of
them), they are schema-versioned (`kSessionSchemaVersion = 2`) with tested migration
(`app/tests/test_persist_chain_migration.cpp`), and they cover everything an undo needs to restore:
the node graph, op chain and base params, mappings, tracks, clips and their notes, the FX chain, and
view state.

So undo is, in essence:

```
push(session_to_json(...))      on mutation
session_from_json(pop(), ...)   on Cmd+Z
```

**Reuse this. Do not write a second serializer.** A parallel undo-state representation would drift
from the persisted one, and the drift would be silent.

**The canvas is already shared.** `app/src/ui/node_canvas.h` is included by *both*
`app/src/ui/node_graph.cpp` and `app/src/ui/audio_node_graph.cpp`. Selection, marquee, and group-drag
belong there — written once, appearing in both graphs. That is precisely what that file exists for.

### What classic learned that we should not re-learn

Classic ships snapshot undo (`src/runtime/core/undo_manager.{h,cpp}`: 200-deep labeled JSON
snapshots, `push(json, replaceTop)`) — and then wrote
`docs/plans/deterministic-grouped-undo.md` about what's wrong with it. Two lessons:

1. **Coalescing on a wall clock is a bug.** Classic merges snapshots that share a param key within
   300 ms. A frame hitch mid-drag splits one gesture into two undo entries; a fast bulk action merges
   two intentional ones. Worse, a bulk action across *distinct* params (clear a 16-step pattern) does
   not merge at all and costs 16 Cmd+Z presses.
2. **Snapshot undo is nonetheless the right size.** Classic's plan explicitly declines to convert to
   command/delta undo, calling it "a separate, much larger rearchitecture." It is right. Snapshots
   are O(session) per entry, which for a session of this scale is nothing, and they are trivially
   correct — there is no inverse-operation to get wrong.

We adopt the model and skip the mistake.

## Decision

1. **One `UndoManager`**, holding labeled `nlohmann::json` session snapshots, ~200 deep, owned by
   `App` (`app/src/app/app.h`). One stack for the whole document — not one per graph. A session *is*
   one document; the split into two graphs is a view concern, and a user pressing Cmd+Z means "undo
   the last thing I did," not "undo the last thing I did in this pane."

2. **Explicit grouping is canonical.** `begin_undo_group(label)` / `end_undo_group()` bracket a
   gesture or bulk action into exactly one entry. A slider drag brackets on mouse-down/mouse-up. A
   marquee move brackets on drag start/end. A timer-based coalesce survives **only** as a fallback
   for callers that cannot bracket — chiefly MCP clients issuing rapid `set_node_param` calls.

3. **Every mutation routes through one hook.** A single `App::mutate(label, fn)` (or equivalent)
   seam is where the snapshot is taken. This matters beyond undo: [ADR-0018](ADR-0018-a-bad-operator-must-not-cost-you-your-work.md)'s
   dirty flag and autosave ride the *same* hook. Installing it once is the whole reason this ADR
   comes first.

4. **Selection is a set.** `sel_op_` and `sel_node_` become `std::set<int>`. Marquee, shift-click,
   group drag, Cmd+C / Cmd+V / Cmd+D, and Delete/Backspace live in `app/src/ui/node_canvas.h`. Paste
   re-keys node ids and preserves connections *internal to the pasted set* (a wire to a node that
   wasn't copied is dropped, not dangled).

5. **Surfaces:** Cmd+Z / Cmd+Shift+Z in `app/src/app/input.cpp`; Edit-menu items in
   `app/src/platform/menu_bar.mm` showing the undo **label** ("Undo Delete Node"); `undo` / `redo`
   control methods in `app/src/cli/`.

### Boundary rule — what this is not

- **Not command/delta undo.** No inverse operations, no operation log. If the session grows to where
  snapshot cost is measurable, that is a future ADR with evidence attached, not a guess now.
- **Not undo for transport or playback state.** Pressing play is not an edit. Snapshots capture the
  document, not the performance.
- **Not cross-session undo.** The stack dies with the process. Recovering *unsaved work* across a
  crash is ADR-0018's job, and it is a different mechanism.

## Consequences

**Good.** Exploration stops being a commitment. Multi-select makes graphs of real size buildable.
Both graphs get all of it from one implementation in the shared canvas. The mutation hook we are
forced to build is exactly the hook ADR-0018 needs.

**Costs.** Every mutation site must be routed through the hook — that is the bulk of the work, and
it is unglamorous. Any site that is missed produces a *silently* wrong undo (an edit that Cmd+Z
skips over), which is worse than no undo, so U2 needs to be exhaustive rather than fast. Snapshot
undo also holds ~200 full session JSONs in memory; at current session sizes this is negligible, but
it is a real number and should be sanity-checked, not assumed.

**Risk.** The one design trap is taking the snapshot *after* the mutation instead of before. Undo
must restore the state you were in, which means the snapshot is captured on entry to the hook.

## Implementation

### U1 — `UndoManager` on the persist round-trip

`app/src/app/undo_manager.{h,cpp}`: `push(json, label, replace_top)`, `undo()`, `redo()`,
`can_undo()`, `undo_label()`. Pure data — no `App`, no GPU — so it is headless-testable, mirroring
the `runtime_health` / `control_parse` pure-core split the codebase already uses. Wire it to exactly
one mutation path end to end.

*Verify:* `app/tests/test_undo_manager.cpp` — push/undo/redo ordering, depth cap eviction,
`replace_top` coalescing, and a **full round-trip** asserting that
`session_from_json(session_to_json(s))` restores an edited graph to its prior state.

### U2 — Route every mutation through the hook

Add the `App::mutate(label, fn)` seam. Route: node add/remove/rename/move, connect/disconnect, param
set (UI *and* MCP), mapping connect/disconnect, track add/remove, FX chain edits, op-chain edits.
Add `begin_undo_group` / `end_undo_group` and bracket the known gestures. Demote the coalesce timer
to the MCP fallback.

*Verify:* headless test driving each mutation via the control-server dispatch and asserting one undo
entry per logical action, and that Cmd+Z restores the prior JSON exactly. **Audit for completeness**
— grep every `vg_->`, `session_*`, and `MappingRegistry` write site and confirm it goes through the
hook. A missed site is a silent bug.

### U3 — Selection as a set

`sel_op_` / `sel_node_` → `std::set<int>`. Marquee, shift-click toggle, group drag in
`app/src/ui/node_canvas.h`. Inspector targets the set's single element when `size() == 1` and shows a
multi-selection state otherwise.

*Verify:* run the app. Marquee-select three nodes in the visual graph, drag them as a group, confirm
the same gestures work in the audio graph with no additional code. Confirm the inspector still
behaves for a single selection.

### U4 — Copy / paste / duplicate / delete + surfaces

Cmd+C / Cmd+V / Cmd+D, Delete/Backspace, all bracketed as single undo groups. Paste re-keys ids and
preserves internal connections. Edit menu with labels. `undo` / `redo` control methods + MCP parity.

*Verify:* run the app — copy a three-node subgraph with wires, paste it, confirm the wires came with
it and that one Cmd+Z removes the entire paste. Confirm `mcp/tests/test_mcp_parity.py` still passes
with the two new methods.
