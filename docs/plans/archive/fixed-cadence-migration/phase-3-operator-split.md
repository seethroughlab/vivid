# Phase 3: Split True Twins and Collapse Fake Dual-Cadence Operators

## Summary

Remove the public dual-cadence operator surface before the runtime break. Operators that are genuinely meaningful in both worlds become explicit `_fr` / `_au` pairs. Operators that only looked dual-cadence because the old runtime made that easy collapse to honest frame-only operators.

## Implementation Changes

### Paired operators

These become explicit public pairs with no bare aliases:

| Existing | Frame | Audio |
|----------|-------|-------|
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

Per paired operator:
- extract shared logic into `*_core.h` only when it meaningfully reduces duplication
- `_fr` variant implements only `FrameProcessable`
- `_au` variant implements only `AudioProcessable`
- audio variant reads modulation from `input_buffers[]` per-sample, not from float CV side channels
- remove the old bare-name registration entirely

### Frame-only collapses

These keep their current bare names and lose `AudioProcessable`:

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

Per collapsed operator:
- remove `AudioProcessable` from inheritance
- delete `process_audio()`
- keep existing bare registration

Essential paths:
- `operators/control/`
- `operators/shared/sequencer/`
- `src/operator_api/operator.h`

## Test Plan

- All `_fr` / `_au` names register and load correctly
- No paired operator still exports a bare name
- All collapsed operators are frame-only
- Grep over `operators/` confirms no core operator still implements both `FrameProcessable` and `AudioProcessable`
- Add or update operator-loader tests so both naming families are validated

## Assumptions and Defaults

- Pairing is semantic, not historical
- No operator is kept dual-cadence "for convenience"
- `_fr` means fixed frame execution world
- `_au` means fixed audio execution world
- suffixes express execution world only and are not optional aliases
- No bare aliases remain for paired operators after this phase
