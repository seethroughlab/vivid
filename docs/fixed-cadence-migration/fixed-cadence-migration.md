# Fixed-Cadence Migration

## Summary

Vivid is moving from implicit dual-cadence operators to a fixed-cadence model:

- every public operator runs in exactly one execution world
- frame/audio crossings are explicit bridge edges
- lanes remain the multiplicity model
- paired operators use `_au` / `_fr` suffixes only when both forms are genuinely needed

This directory breaks the migration into seven implementation phases. Phases 1-3 are preparatory and can land incrementally. Phases 4-6 are the architectural break and should be treated as one coordinated cutover. Phase 7 updates the user-facing surfaces and docs to match.

## Operator Classification

### 20 paired operators -> explicit `_fr` / `_au` names

These remain one semantic concept with two legitimate execution worlds. The bare names are removed from the active surface.

| Existing | Frame variant | Audio variant |
|----------|---------------|---------------|
| `clock` | `clock_fr` | `clock_au` |
| `envelope` | `envelope_fr` | `envelope_au` |
| `lfo` | `lfo_fr` | `lfo_au` |
| `smooth` | `smooth_fr` | `smooth_au` |
| `mseg` | `mseg_fr` | `mseg_au` |
| `sample_hold` | `sample_hold_fr` | `sample_hold_au` |
| `gate` | `gate_fr` | `gate_au` |
| `quantizer` | `quantizer_fr` | `quantizer_au` |
| `step_counter` | `step_counter_fr` | `step_counter_au` |
| `euclidean` | `euclidean_fr` | `euclidean_au` |
| `step_seq` | `step_seq_fr` | `step_seq_au` |
| `pattern_seq` | `pattern_seq_fr` | `pattern_seq_au` |
| `sequencer` | `sequencer_fr` | `sequencer_au` |
| `note_pattern` | `note_pattern_fr` | `note_pattern_au` |
| `chord_progression` | `chord_progression_fr` | `chord_progression_au` |
| `arpeggiator` | `arpeggiator_fr` | `arpeggiator_au` |
| `drum_sequencer` | `drum_sequencer_fr` | `drum_sequencer_au` |
| `tracker` | `tracker_fr` | `tracker_au` |
| `phase_to_midi` | `phase_to_midi_fr` | `phase_to_midi_au` |
| `drum_kit` | `drum_kit_fr` | `drum_kit_au` |

### 11 operators collapse to frame-only and keep bare names

These were only "dual-cadence" because the old runtime made that convenient. They should become honest frame/control operators.

- `alternate`
- `repeat`
- `tile`
- `select`
- `stack`
- `math`
- `logic`
- `macro`
- `modulated_gain`
- `note_duration`
- `path_animate`

## Phase Plans

### [Phase 1: Add Explicit Bridge Metadata to the Graph Model](phase-1-graph-model.md)

Add `bridge` to the graph schema and `BridgeKind` to compiled edges without changing runtime behavior yet. This is additive prep work so later phases can compile explicit bridge intent without changing execution semantics early.

### [Phase 2: Rename Port Types to SCALAR and AUDIO_BUFFER](phase-2-port-rename.md)

Mechanically rename the numeric port kinds across the repo while keeping their integer values unchanged. This is a large but mostly lexical phase, and it intentionally does not change compatibility rules, cadence inference, or bridge behavior yet.

### [Phase 3: Split True Twins and Collapse Fake Dual-Cadence Operators](phase-3-operator-split.md)

Remove the public dual-cadence operator surface. True twins become explicit `_fr` / `_au` pairs, while frame-native operators lose `AudioProcessable` and keep their current bare names.

### Phase 4: Remove Dual-Cadence Infrastructure (3 sub-phases)

Delete the core dual-cadence model incrementally. 4A and 4B land independently; 4C is coordinated with Phase 5.

#### [Phase 4A: Remove Cadence Override Surface and Delete Inference Pass](phase-4a-cadence-override-removal.md)

Remove the cadence override mechanism and the promotion pass. Dead code since Phase 3. Safe, no behavioral change.

#### [Phase 4B: Remove VividCadenceCapability, Signal Ordinals, and Enforce Explicit Bridges](phase-4b-capability-ordinals-bridge-enforcement.md)

Remove the dual-cadence type system, scalar CV routing ordinals, and Phase 2 aliases. Enforce explicit bridge rules in the compiler. Bump operator ABI.

#### [Phase 4C: Remove Float-CV Plumbing and Update Audio Operators](phase-4c-float-cv-removal.md)

Remove `input_float_values`/`output_float_values` from VividAudioContext. Update all audio operators. Lands atomically with Phase 5.

### [Phase 5: Replace CadenceBridge With Explicit AudioFrameBridge Semantics](phase-5-bridge-executor-rework.md)

Refactor the bridge and executors around explicit bridge payloads keyed by node and port, not signal ordinals or reused scalar output paths. This makes bridge execution match the graph model introduced in earlier phases.

### [Phase 6: Migrate Graphs and Replace the Test Suite Around the New Model](phase-6-graph-migration-and-tests.md)

Migrate first-party graphs and package graphs to explicit bridge edges and the new `_fr` / `_au` operator names, then rewrite the tests so the fixed-cadence model becomes the only actively validated architecture.

### [Phase 7: Update UI, Control Server, MCP, and Docs for the Fixed-Cadence Model](phase-7-ui-mcp-docs.md)

Update the user-facing surfaces to match runtime truth. This phase also locks the `_fr` / `_au` naming convention into UI and docs. It targets the actual repo surfaces:

- `src/runtime/control_server.cpp`
- `mcp/vivid_mcp.py`

and the node graph UI/docs, not a nonexistent CLI MCP server.

## Sequencing

```text
Phase 1  (graph model)              ─── additive, non-breaking
Phase 2  (port rename)              ─── mechanical, can land independently
Phase 3  (operator split)           ─── depends on Phase 2 naming
Phase 4A (cadence override removal) ─── safe cleanup, no behavioral change
Phase 4B (capability + ordinals)    ─── structural break, ABI bump
Phase 4C (float-CV removal)         ─┐
Phase 5  (bridge/executor)           ├─ atomic cutover
Phase 6  (migration/tests)          ─┘
Phase 7  (UI/MCP/docs)              ─── after Phase 6, partly parallelizable
```

Phases 1-3 are safe preparation. Phases 4-6 are the actual architectural migration and should be treated as one coordinated break. Phase 7 makes the new model visible and teachable everywhere else.

## Readiness Review

- [Phase 3 Review Feedback](phase-3-review-feedback.md)
- [Phase 5 Review Feedback](phase-5-review-feedback.md)
