# Operator Timing Redesign

15 control operators need individual timing redesign before they can become dual-cadence (audio-capable). Their current timing models assume frame-rate `dt` (~16ms) and break at audio-rate `dt` (~5µs per sample, ~5ms per 256-sample buffer).

## The problem

These operators track time at **call rate**: beat-phase wraps, step transitions, gate windows, and envelope stages all fire once per `process()` call. At frame rate (~60Hz), this is fine. At audio rate (~187 callbacks/sec for 256-sample buffers at 48kHz), timing compresses by ~3x — and at per-sample rate (~48kHz), by ~800x.

The result: envelope durations compress, sequencer steps blur together, gate edges lose musical timing, trigger detection becomes meaningless.

## The target pattern

**`state_machine`** is the reference implementation. It uses beat-phase wrap detection for all timing decisions:

```cpp
float delta = beat_phase - prev_phase_;
if (delta < -0.5f) { /* beat wrapped — advance state */ }
prev_phase_ = beat_phase;
```

This is cadence-agnostic: it reacts to an external phase signal regardless of how often it's called. The state advances when the phase wraps, not when a timer expires.

**Two proven patterns** from the existing dual-cadence migration:

1. **Stateless transform** (e.g. `math`, `quantizer`): Same computation at any cadence. No time, no phase.
2. **Phase-driven state machine** (e.g. `state_machine`, `step_counter`): All timing reacts to phase wraps or trigger edges from an external signal.

Operators below need to move from pattern **"internal time accumulation"** to pattern **2**.

---

## Time-accumulating operators

These accumulate internal time (`elapsed += dt`) which changes behavior at audio rate because dt shrinks.

### mseg
**Status:** Done
**Current timing:** Gate-triggered envelope with `elapsed_ += dt`. Stage machine (IDLE → PLAYING → LOOPING → RELEASING). Loop and release durations are absolute (seconds).
**State:** `elapsed_`, `stage_`, `release_start_value_`, `current_value_`, `prev_gate_`
**Ports:** `gate` input (trigger), `beat_phase` input (unused for core timing)
**Problem:** Originally thought to need phase-driven redesign.
**Redesign approach:** `delta_time` is provided correctly by the runtime at both cadences, so `elapsed_ += dt` works as-is. Added `process_audio()` with shared `compute()` helper.
**Notes:** Same pattern as `path_animate` — delta_time naturally scales. No timing redesign needed.

### path_animate
**Status:** Done
**Current timing:** Free-running `free_phase_ += delta_time * speed`, with optional `phase_in` override. Loop/ping-pong modes.
**State:** `free_phase_`, `finished_`
**Ports:** `phase_in` (optional override), `speed` param
**Problem:** Speed parameter assumes frame-rate traversal.
**Redesign approach:** `delta_time` is provided correctly by the runtime at both cadences, so free-running accumulation works as-is. Added `process_audio()` with shared `compute()` helper.
**Notes:** Same pattern as `spread_noise` — `delta_time` naturally scales.

### random
**Status:** Removed — dead code, superseded by LFO sample_hold/smooth_random modes and RandomSH.

### random_sh
**Status:** Removed — features consolidated into LFO (slew, gaussian distribution, gate-triggered S&H, seed).

---

## Beat/sequence operators

These produce gates, steps, or MIDI-adjacent behavior. They mostly use beat-phase wrap detection already but apply it at call boundaries. The fix pattern is the same: ensure step transitions, gate windows, and output updates happen correctly when called at audio rate.

### arpeggiator
**Status:** Done
**Current timing:** Beat tracking via `delta < -0.5f` → `beat_count_++`. Rate multipliers scale beats to arp steps. Swing per step-pair.
**State:** `beat_count_`, `prev_phase_`, `step_offset_`, latch buffer, RNG
**Ports:** `beat_phase` (required), notes/velocities/gates spreads
**Problem:** Was audio-only (`AudioOperatorBase`), not dual-cadence.
**Redesign approach:** Added `FrameProcessable` and shared `compute()` helper. Phase-driven logic already cadence-agnostic.
**Notes:** Despite being the most complex sequencer, the migration was mechanical.

### chord_progression
**Status:** Done
**Current timing:** Beat tracking + step selection via `(beat_count_ / beats_per_step) % num_steps`. Gate from `beat_phase < gate_length`.
**State:** `beat_count_`, `prev_phase_`, MIDI note buffers
**Ports:** `beat_phase` (required)
**Problem:** Was audio-only (`AudioOperatorBase`), not dual-cadence.
**Redesign approach:** Added `FrameProcessable` and shared `compute()` helper. Same pattern as `note_pattern`.
**Notes:** Migrated from audio-only → dual-cadence.

### drum_sequencer
**Status:** Done
**Current timing:** Phase-driven with beat_phase + reset inputs, swing, per-step triggers.
**State:** `prev_step_`, `phase_offset_`, `prev_reset_`, MIDI buffer
**Ports:** `beat_phase`, `reset` (inputs), 6 drum gates + step + mod outputs + spreads + MIDI
**Problem:** Was audio-only (`AudioOperatorBase`), not dual-cadence. Doc incorrectly stated "no timing mechanism."
**Redesign approach:** Added `FrameProcessable` and shared `compute()` helper. Already phase-driven.
**Notes:** Doc was outdated — operator already had beat_phase input and phase-driven step logic.

### euclidean
**Status:** Done
**Current timing:** Beat tracking + Bjorklund pattern. Rate multipliers. Gate from `step_phase < gate_length`.
**State:** `beat_count_`, `prev_phase_`, `prev_step_`, `pattern_[]`
**Ports:** `beat_phase` (required)
**Problem:** Gate output at call boundary.
**Redesign approach:** Added `process_audio()` with shared `compute()` helper. Beat tracking and gate window logic already cadence-agnostic.
**Notes:** Same pattern as `alternate`.

### note_pattern
**Status:** Done
**Current timing:** Beat tracking + step selection. Gate from `beat_phase < gate_length`.
**State:** `beat_count_`, `prev_phase_`, MIDI buffers
**Ports:** `beat_phase` (required)
**Problem:** Was audio-only (`AudioOperatorBase`), not dual-cadence.
**Redesign approach:** Added `FrameProcessable` and shared `compute()` helper. Beat tracking, chord voicing, gate window, and MIDI edge detection already cadence-agnostic.
**Notes:** Migrated from audio-only → dual-cadence (opposite direction from most).

### pat_transform
**Status:** Done
**Current timing:** Stateless pattern transformation (reverse, rotate, scale, offset, probability).
**State:** None
**Ports:** Input/output spreads only
**Problem:** Operates on entire pattern array at once.
**Redesign approach:** Trivially audio-capable — stateless spread transform. Added `process_audio()` with shared `compute()` helper, same pattern as `stack`.
**Notes:** No timing redesign needed — was misclassified.

### pattern_seq
**Status:** Done
**Current timing:** Beat tracking + rate multipliers + per-step probability.
**State:** `beat_count_`, `prev_phase_`, `prev_step_`, `prev_gate_`, MIDI tracking, RNG
**Ports:** `beat_phase` (required)
**Problem:** Was audio-only (`AudioOperatorBase`), not dual-cadence.
**Redesign approach:** Added `FrameProcessable` and shared `compute()` helper. Phase-driven logic already cadence-agnostic.
**Notes:** Same pattern as `note_pattern`/`chord_progression`.

### phase_to_midi
**Status:** Done
**Current timing:** Phase-wrap detection → MIDI note-on.
**State:** `prev_phase_`, `midi_buf_`
**Ports:** `beat_phase` (required), `midi_out`
**Problem:** One MIDI event per wrap, at call boundary.
**Redesign approach:** Wrap detection is already cadence-agnostic. Added `process_audio()` with shared `compute()` helper.
**Notes:** No timing redesign needed — phase-wrap logic works at any cadence.

### sequencer
**Status:** Done
**Current timing:** Phase modulo with reset handling. Step detection via scaled phase. Ratchet subdivision.
**State:** `prev_step_`, `phase_offset_`, `prev_reset_`, `step_active_`, `current_ratchet_`, RNG, MIDI tracking
**Ports:** `beat_phase`, `reset` (required)
**Problem:** Was audio-only (`AudioOperatorBase`), not dual-cadence.
**Redesign approach:** Added `FrameProcessable` and shared `compute()` helper. Purely phase-driven logic already cadence-agnostic.
**Notes:** Same pattern as `pattern_seq`.

### step_seq
**Status:** Done
**Current timing:** Dual-mode: free-running `free_phase_ += dt * frequency` or sync `phase = fmod(beat_phase * frequency, 1.0)`. Glide interpolation.
**State:** `free_phase_`, `prev_step_`, `current_value_`
**Ports:** `gate`, `beat_phase` (for sync mode)
**Problem:** Phase accumulation per call. Glide filter uses call-rate dt.
**Redesign approach:** `delta_time` is provided correctly at both cadences. Glide runs once per call — slightly smoother at audio rate but functionally correct. Added `process_audio()` with shared `compute()` helper.
**Notes:** Same pattern as `path_animate`/`mseg` — delta_time naturally scales.

### tracker
**Status:** Done
**Current timing:** Beat tracking → tick/row/pattern sequencing. Speed parameter multiplies rate. Order list advances on song progression.
**State:** `beat_count_`, `prev_phase_`, `current_row_`, `current_order_`, `current_tick_`, per-channel note-off tracking, mute mask
**Ports:** `beat_phase`, `reset` (required)
**Problem:** Was audio-only (no `FrameProcessable`), not dual-cadence.
**Redesign approach:** Added `FrameProcessable` and shared `compute()` helper. Phase-driven tick calculation already cadence-agnostic.
**Notes:** Despite being the most complex operator, the migration was mechanical.

---

## Suggested migration order

Based on complexity and likelihood of being trivially portable:

1. **pat_transform** — likely just needs `process_audio()` added (stateless)
2. **phase_to_midi** — simple wrap detection, may just work
3. **euclidean** — simple pattern + gate window
4. **note_pattern** — beat tracking + gate window
5. **chord_progression** — similar to note_pattern
6. **path_animate** — already has phase_in override
7. ~~**random** — removed (dead code, superseded by LFO/RandomSH)~~
8. **random_sh** — phase-driven + slew redesign
9. **mseg** — envelope phase redesign
10. **step_seq** — dual-mode + glide
11. **pattern_seq** — beat tracking + probability
12. **sequencer** — phase modulo + ratchet
13. **drum_sequencer** — needs timing model added from scratch
14. **arpeggiator** — most complex arp logic
15. **tracker** — most complex overall
