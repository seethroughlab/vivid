# Phase 4C: Remove Float-CV Plumbing and Update Audio Operators

## Summary

Remove `input_float_values` and `output_float_values` from `VividAudioContext` and all supporting infrastructure. Update all `_au` operators to receive modulation through params (synced by `VIVID_REGISTER`) or audio input buffers instead of the float CV side channel. This sub-phase lands atomically with Phase 5 (bridge executor rework).

## Implementation Changes

### Remove from VividAudioContext

- In `src/operator_api/types.h`:
  - remove `input_float_values` and `output_float_values` fields from `VividAudioContext`

### Remove from AudioNodeState

- In `src/runtime/compiled_graph.h`:
  - remove `float_input_values`, `float_output_values`
  - remove `float_input_defaults`, `float_input_scratch`, `float_output_scratch`
  - remove `float_input_count`, `float_output_count`

### Remove from audio executor

- In `src/runtime/audio_executor.cpp`:
  - remove `input_float_values` / `output_float_values` assignment on VividAudioContext
  - remove float output extraction logic (signal_output_extractions loop)
  - remove float CV routing between audio nodes (ordinal-based copy)

### Remove from cadence bridge

- In `src/runtime/cadence_bridge.cpp`:
  - remove float CV input paths in `push_to_audio()` (SCALAR → float_input_values)
  - remove float output paths in `pull_from_audio()` (float_output_values → frame input)

### Remove from embedded operator support

- In `src/operator_api/embedded_op.h` / `child_op.h`:
  - remove float CV references

### Update _au operators

All `_au` operator variants that read `ctx->input_float_values[N]` must be updated.

**For simple _au operators** (gate_au, quantizer_au, sample_hold_au, step_counter_au, clock_au, drum_kit_au, phase_to_midi_au, euclidean_au, mseg_au, pattern_seq_au, step_seq_au):
- Replace `ctx->input_float_values[N]` with member param reads (already synced by `VIVID_REGISTER` macro before `process_audio()` is called)
- For per-sample modulation: read from `ctx->input_buffers[N]` if the port type supports it

**For delegation-pattern _au operators** (lfo_au, envelope_au):
- The delegated `impl_.process_audio(ctx)` calls the original code which reads `input_float_values`
- Rewrite the original audio process methods to use params/buffers instead
- Or: inline the audio logic directly instead of delegating

**For _core.h-based _au operators** (sequencer_au, note_pattern_au, chord_progression_au, arpeggiator_au, drum_sequencer_au, tracker_au, smooth_au):
- Update the `process_audio()` method in each `_au.cpp` to not use float CV

**For audio-native operators** (oscillator, gain, filter, etc.):
- These also read `input_float_values` for CV modulation
- Update to read from params or input buffers via the new Phase 5 bridge delivery mechanism

Essential paths:
- `src/operator_api/types.h`
- `src/runtime/compiled_graph.h`
- `src/runtime/audio_executor.cpp`
- `src/runtime/cadence_bridge.cpp`
- 60+ operator source files

## Test Plan

- Rewrite `test_signal_port.cpp` for the new model
- Update `test_audio_correctness.cpp` and `test_modulation_ops.cpp`
- Verify audio operators still produce correct output via params
- Grep cleanup gate: no `input_float_values` or `output_float_values` in active runtime

## Assumptions and Defaults

- Phase 4A and 4B have already landed
- This sub-phase lands atomically with Phase 5 (bridge executor rework)
- After this phase, there is no float CV side channel — all cross-cadence data flows through explicit bridge edges
- The `VIVID_REGISTER` macro param sync (`_vivid_sync_params`) is the primary mechanism for delivering scalar values to audio operators
