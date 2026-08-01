# Phase 5 — agent scenario + failure transcript (control server :9898, neon loaded)

## Successful scenario: inspect → explain → edit → verify → undo

1. INSPECT — inspect_session_overview →
   {counts:{mappings:1, scenes:5, tracks:3, visual_nodes:9}, project:{path:.../neon},
    summary:"3 tracks, 5 scenes at 124 BPM, playing; visuals have 9 op nodes ..."}

2. EXPLAIN — explain_signal_flow →
   ["3 tracks, 5 scenes at 124 BPM, playing; visuals have 9 op nodes and 1 mappings",
    "transport.beat_pulse drives node:4.size amount=0.18 range=[0.22,0.4]"]

3. EDIT — map_audio_to_visual_param{source:master, characteristic:level, node_id:4,
   param:spread, amount:0.5, lo:0.2, hi:0.9} → summary:"master level drives Instancer.spread"

4. VERIFY — get_mappings → 2 mappings (was 1)

5. UNDO — undo → {did:true, redo_label:"Connect Mapping"}; get_mappings → 1 mapping
   (agent edit entered the SAME undo history as a UI edit)

## Failure paths (structured + honest)

- invalid node id — map ...node_id:999 → not_found "no visual node with node_id 999"
- bad package     — install_operator_package /no/such/pkg → bad_arg "no vivid-package.json in /no/such/pkg"
- unknown method  — frobnicate → unknown_method "unknown method: frobnicate"
- permissive edit — connect_nodes{node_id:4,input_id:4} (self-loop) → ok:true (accepted; cycle-safe topo)
