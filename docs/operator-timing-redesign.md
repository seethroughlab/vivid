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
**Status:** Not started
**Current timing:** Gate-triggered envelope with `elapsed_ += dt`. Stage machine (IDLE → PLAYING → LOOPING → RELEASING). Loop and release durations are absolute (seconds).
**State:** `elapsed_`, `stage_`, `release_start_value_`, `current_value_`, `prev_gate_`
**Ports:** `gate` input (trigger), `beat_phase` input (unused for core timing)
**Problem:** At audio rate, `dt` is ~5µs per sample — envelope plays through 800x faster.
**Redesign approach:** Replace internal `elapsed_` with phase-driven position. Option A: normalize segment durations to [0,1] and use external phase input for playback position. Option B: scale durations by cadence (multiply by sample_rate/frame_rate ratio). Option A is cleaner.
**Notes:**

### path_animate
**Status:** Not started
**Current timing:** Free-running `free_phase_ += delta_time * speed`, with optional `phase_in` override. Loop/ping-pong modes.
**State:** `free_phase_`, `finished_`
**Ports:** `phase_in` (optional override), `speed` param
**Problem:** Speed parameter assumes frame-rate traversal — speed=1.0 means "traverse once per second" at frame rate but runs 800x at audio rate per-sample.
**Redesign approach:** Already has `phase_in` override port — when audio-capable, require external phase input instead of free-running accumulation. OR: make `speed` cadence-aware (divide by callbacks-per-second).
**Notes:** Simplest of the four — `phase_in` override is already the right model.

### random
**Status:** Not started
**Current timing:** Trigger-edge detection (`prev_trigger_`) or free-run mode. Generates one random value per call.
**State:** `rng_state_`, `current_value_`, `prev_trigger_`, `seeded_`
**Ports:** `trigger` input (optional)
**Problem:** At audio rate, trigger edges fire every sample (no meaningful "edge"). Free-run generates a new value every sample — white noise, not random modulation.
**Redesign approach:** Phase-driven: advance RNG state on phase wraps (like beat_phase wrap detection). Between wraps, hold current value. This gives musically meaningful random modulation at any cadence.
**Notes:**

### random_sh
**Status:** Not started
**Current timing:** Timed mode: `phase_ += dt * rate`. Triggered mode: gate-edge detection. Slew interpolation between held values.
**State:** `phase_`, `rng_state_`, `target_value_`, `current_value_`, `prev_gate_`, `seeded_`
**Ports:** `beat_phase`, `gate` inputs
**Problem:** Phase accumulation uses single `dt` per call. Slew filtering uses call-rate `dt`, not per-sample smoothing.
**Redesign approach:** Timed mode: use beat_phase wraps instead of internal phase accumulation. Triggered mode: keep gate-edge detection (works at audio rate if gate signal is audio-rate). Slew: per-sample exponential smoothing with `1 - exp(-dt * rate)`.
**Notes:**

---

## Beat/sequence operators

These produce gates, steps, or MIDI-adjacent behavior. They mostly use beat-phase wrap detection already but apply it at call boundaries. The fix pattern is the same: ensure step transitions, gate windows, and output updates happen correctly when called at audio rate.

### arpeggiator
**Status:** Not started
**Current timing:** Beat tracking via `delta < -0.5f` → `beat_count_++`. Rate multipliers scale beats to arp steps. Swing per step-pair.
**State:** `beat_count_`, `prev_phase_`, `step_offset_`, `arp_direction_`, latch buffer, RNG
**Ports:** `beat_phase` (required), notes/velocities/gates spreads
**Problem:** Step selection once per call. Gate window (`step_phase < gate_length`) checked once.
**Redesign approach:** Already phase-driven — needs per-sample phase evaluation for gate window output. Extract `compute(beat_phase, ...)` that returns gate/note/velocity for a given phase.
**Notes:** Most complex sequencer. May benefit from being done last after simpler ones prove the pattern.

### chord_progression
**Status:** Not started
**Current timing:** Beat tracking + step selection via `(beat_count_ / beats_per_step) % num_steps`. Gate from `beat_phase < gate_length`.
**State:** `beat_count_`, `prev_phase_`, MIDI note buffers
**Ports:** `beat_phase` (required)
**Problem:** Gate timing at call boundary.
**Redesign approach:** Same as arpeggiator — per-sample gate window evaluation.
**Notes:**

### drum_sequencer
**Status:** Not started
**Current timing:** No beat_phase input (`kTimeDependent = false`). Pattern is purely parameter-driven.
**State:** Pattern parameters only
**Ports:** None
**Problem:** No timing mechanism at all — can't respond to dynamic tempo.
**Redesign approach:** Add `beat_phase` input. Use beat-phase wrap detection for step advancement. This is a more fundamental redesign than the others.
**Notes:** May need the most design work since it currently has no timing model.

### euclidean
**Status:** Not started
**Current timing:** Beat tracking + Bjorklund pattern. Rate multipliers. Gate from `step_phase < gate_length`.
**State:** `beat_count_`, `prev_phase_`, `prev_step_`, `pattern_[]`
**Ports:** `beat_phase` (required)
**Problem:** Gate output at call boundary.
**Redesign approach:** Per-sample gate window evaluation. Pattern itself is stateless (Bjorklund algorithm) — only gate output needs audio-rate precision.
**Notes:** Good candidate for early migration — relatively simple.

### note_pattern
**Status:** Not started
**Current timing:** Beat tracking + step selection. Gate from `beat_phase < gate_length`.
**State:** `beat_count_`, `prev_phase_`, MIDI buffers
**Ports:** `beat_phase` (required)
**Problem:** Gate timing at call boundary.
**Redesign approach:** Per-sample gate evaluation.
**Notes:**

### pat_transform
**Status:** Done
**Current timing:** Stateless pattern transformation (reverse, rotate, scale, offset, probability).
**State:** None
**Ports:** Input/output spreads only
**Problem:** Operates on entire pattern array at once.
**Redesign approach:** Trivially audio-capable — stateless spread transform. Added `process_audio()` with shared `compute()` helper, same pattern as `stack`.
**Notes:** No timing redesign needed — was misclassified.

### pattern_seq
**Status:** Not started
**Current timing:** Beat tracking + rate multipliers + per-step probability.
**State:** `beat_count_`, `prev_phase_`, `prev_step_`, `prev_gate_`, MIDI tracking, RNG
**Ports:** `beat_phase` (required)
**Problem:** Step/gate transitions at call boundary. Probability computed once per step.
**Redesign approach:** Per-sample gate evaluation. Probability stays per-step (intentional — re-rolling per sample would be wrong).
**Notes:**

### phase_to_midi
**Status:** Not started
**Current timing:** Phase-wrap detection → MIDI note-on.
**State:** `prev_phase_`, `midi_buf_`
**Ports:** `beat_phase` (required), `midi_out`
**Problem:** One MIDI event per wrap, at call boundary.
**Redesign approach:** At audio rate, wrap detection is already sample-accurate. May just need `process_audio()` added directly.
**Notes:** Likely simple — similar to `pat_transform`, may not need redesign.

### sequencer
**Status:** Not started
**Current timing:** Phase modulo with reset handling. Step detection via scaled phase. Ratchet subdivision.
**State:** `prev_step_`, `phase_offset_`, `prev_reset_`, `step_active_`, `current_ratchet_`, RNG, MIDI tracking
**Ports:** `beat_phase`, `reset` (required)
**Problem:** Step boundaries and ratchet division at call boundary.
**Redesign approach:** Per-sample step/ratchet evaluation. Core logic already phase-driven — just needs to run at audio rate.
**Notes:**

### step_seq
**Status:** Not started
**Current timing:** Dual-mode: free-running `free_phase_ += dt * frequency` or sync `phase = fmod(beat_phase * frequency, 1.0)`. Glide interpolation.
**State:** `free_phase_`, `prev_step_`, `glide_target_`, `glide_current_`
**Ports:** `gate`, `beat_phase` (for sync mode)
**Problem:** Phase accumulation per call. Glide filter uses call-rate dt.
**Redesign approach:** Sync mode is already phase-driven. Free-run mode needs cadence-aware frequency scaling. Glide needs per-sample exponential smoothing.
**Notes:**

### tracker
**Status:** Not started
**Current timing:** Beat tracking → tick/row/pattern sequencing. Speed parameter multiplies rate. Order list advances on song progression.
**State:** `beat_count_`, `prev_phase_`, `current_row_`, `current_order_`, `current_tick_`, per-channel note-off tracking, mute mask
**Ports:** `beat_phase`, `reset` (required)
**Problem:** Tick/row/pattern transitions at call boundary.
**Redesign approach:** Per-sample tick evaluation. Most complex sequencer — tracker format with effects column, multi-channel notes.
**Notes:** Most complex of all 15. Do last.

---

## Suggested migration order

Based on complexity and likelihood of being trivially portable:

1. **pat_transform** — likely just needs `process_audio()` added (stateless)
2. **phase_to_midi** — simple wrap detection, may just work
3. **euclidean** — simple pattern + gate window
4. **note_pattern** — beat tracking + gate window
5. **chord_progression** — similar to note_pattern
6. **path_animate** — already has phase_in override
7. **random** — phase-driven RNG redesign
8. **random_sh** — phase-driven + slew redesign
9. **mseg** — envelope phase redesign
10. **step_seq** — dual-mode + glide
11. **pattern_seq** — beat tracking + probability
12. **sequencer** — phase modulo + ratchet
13. **drum_sequencer** — needs timing model added from scratch
14. **arpeggiator** — most complex arp logic
15. **tracker** — most complex overall
