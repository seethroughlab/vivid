# Session View Per-Track Independence

## Context

Vivid's Session view (bottom strip) already does quantized BPM-locked variation switching — but variations are whole-graph snapshots. Switching a variation changes everything simultaneously.

A clip launcher needs per-track independence: swap the bass pattern while drums keep running, launch a new chord sequence without touching the lead. This is the gap between "session view" and "clip launcher."

The good news: the mechanism already exists. **State-preset mapping** (StateMachine node → conditional preset recall on target nodes, with crossfade support) is exactly per-track independence. It just isn't surfaced as a clip grid.

This plan is primarily a **UI project**, not an architecture project.

## How the Existing Mechanism Works

```
[StateMachine] ──state 0──▶ recall preset "A" on [bass_synth]
               ──state 1──▶ recall preset "B" on [bass_synth]
               ──state 2──▶ recall preset "C" on [bass_synth]
```

A StateMachine node has an integer state output (0–N). When it transitions states, the state-preset mapping system automatically recalls specific presets on specific target nodes. Crossfade between presets is supported via `xfade_mode` and `xfade_bars` params.

The graph already has `queue_variation()` with quantized switching. The same quantize logic needs to apply to StateMachine state transitions.

## Target Experience

```
SESSION VIEW (extended)

              Track 1 (Bass)    Track 2 (Chords)   Track 3 (Lead)
  Pattern A   [████ active]     [     ]            [████ active]
  Pattern B   [     ]           [████ active]      [     ]
  Pattern C   [     ]           [     ]            [     ]
  + New

  [Off] [Beat] [Bar] [4Bar]  ← quantize selector (already exists)
```

- Each column = one StateMachine node controlling one "track" (instrument + pattern)
- Each row = one state (one pattern slot)
- Clicking a cell queues that state transition, fires on the next quantize boundary
- The existing variation grid (rows spanning all columns) can coexist — selecting a variation is a scene-level "launch all columns at once"

## Architecture Approach

### Option A: Extend the existing Session view (recommended)
Add a "clip grid" mode to the Session view that displays StateMachine states per node alongside the existing variation rows. The quantize infrastructure is reused — `queue_variation()` already handles beat/bar/4bar firing; add a parallel `queue_state_transition(node_id, state, quantize_mode)` that uses the same tick logic.

### Option B: Per-track variation scoping
Add a `scope` field to variations so a variation only applies to a subset of nodes. Simpler conceptually, but loses the StateMachine crossfade capability and requires variation data model changes.

**Recommendation: Option A.** The StateMachine infrastructure is more powerful (crossfades, arbitrary state counts) and already exists. The cost is that users need a StateMachine node per "track," which can be hidden or auto-created.

## Phased Delivery

### Phase 1 — Quantized StateMachine transitions
- Add `queue_state_transition(node_id, state_idx, quantize_mode)` to `RuntimeAPI`
- Reuse the existing `tick_quantized_switch()` pattern
- MCP tool: `queue_state_transition`
- No UI yet — usable via MCP

**Done when:** An MCP call can queue a StateMachine state change that fires on the next bar boundary.

### Phase 2 — Session view clip grid
- Add a clip grid section to the Session view below (or replacing) the variation strip
- Auto-discover StateMachine nodes that have state-preset mappings (these are the "tracks")
- Render columns per discovered StateMachine, rows per state
- Click to queue transition using the existing quantize mode selector
- Show active state (filled), queued state (pulsing), available states (dim)
- Column header = StateMachine node name (rename via double-click)

**Done when:** A user can launch per-track pattern switches from the Session view UI.

### Phase 3 — Auto-setup workflow
- "Add Track" button in the Session view creates a StateMachine node, wires it to a new `MidiPattern` node (if the MIDI pattern editor exists), and adds it to the clip grid
- Hides the StateMachine node from the main graph view (or groups it)
- "Add Pattern" adds a new state + preset slot to an existing track

**Done when:** A user can build a multi-track clip launcher without manually wiring StateMachine nodes.

## Key Integration Points

| Concern | Where |
|---|---|
| State-preset mapping logic | `src/runtime/control/runtime_api_variations.cpp` |
| Quantized variation switching | Same file — `tick_quantized_switch()`, `PendingVariation` |
| Session view UI | `src/ui/graph/node_graph_draw_elements.cpp` (line ~865) |
| Session UI state | `src/ui/graph/node_graph.h` (line ~830) |
| GraphSnapshot variation data | `src/ui/graph/graph_snapshot.h` (lines 393–402) |
| StateMachine operator | `operators/control/state_machine/` |
| MCP tools | `src/cli/mcp_server.cpp` |

## What This Looks Like End-to-End

A user with CLAP hosting + MIDI pattern editor + this feature can:

1. Have 3 "tracks": Bass (`clap_instrument` + StateMachine), Chords (same), Lead (same)
2. Each StateMachine has 4 states → 4 `MidiPattern` nodes per track (patterns A/B/C/D)
3. Session view shows a 3×4 clip grid
4. Launch patterns independently, BPM-locked, while AV visuals react
5. Switch all tracks at once via a variation (scene launch)

That's a complete live performance setup.

## Key Risks

| Risk | Mitigation |
|---|---|
| StateMachine discovery heuristic | Must reliably identify which StateMachines are "tracks" vs. other uses — use a flag or naming convention |
| Graph snapshot needs new data | Clip grid state (per-StateMachine active/queued state) must flow into `GraphSnapshot` for UI rendering |
| Auto-setup creates complex graphs | Phase 3 grouping/hiding is important for legibility |
