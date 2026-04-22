# Arpeggiator — future features

Companion to `arpeggiator.md`. Everything here is deliberately **deferred** from v1. Each item carries enough design direction that a future adopter (or LLM) can pick it up without re-reading the Cthulhu manual.

The source of inspiration is **Xfer Records' Cthulhu** (arp module only — the chord-memorizer half is its own territory, not something our control-domain Arpeggiator should try to absorb). What Cthulhu calls "graph tabs" are what we call "per-step lanes."

## v1 recap (shipped)

Four lanes × 16 steps:
- **Note Override** — `0` = follow global `mode`, `1..8` = specific pool index, `9` = mute.
- **Velocity** — existing `vel_N`, widened to 16.
- **Transpose** — existing `tr_N`, widened to 16.
- **Gate length** — new `gt_N`, multiplies global gate.

Plus `mod_steps` extended to 16. Inspector retired, dedicated editor window.

## Tier A — cheap and additive (independent lanes)

Each of these is a new per-step lane with its own pure-numeric semantics. Engine change: add a param cluster + one line in `compute()`. Editor change: one more row in the grid. Landing cost: probably ~2 hours each.

### A1. Per-step Octave (±2)

**Param**: `oct_N` (int 0..4 with 2 as the center, or `-2..+2` directly if the param system allows signed defaults). Default 2 (= 0 offset).

**Engine**: after the existing `pool_notes[idx] + tr_mod`, add `+ (oct_N - 2) * 12` semitones.

**Editor**: small centered bipolar bar per cell, similar to Transpose but range is ±2 not ±24. Digit entry maps `0..4` to the 5 octave states.

**Why defer from v1**: genuinely useful but the existing `tr_N` (±24 semitones) already covers ±2 octaves. The split matters for *display clarity* (octave jumps read cleaner than ±12 semitones) but not for capability. Ship when the editor has room to show it without cramping.

**Priority**: Medium. Ship first when we come back.

### A2. Per-step Probability (0..1)

**Param**: `prob_N` (float 0..1, default 1.0).

**Engine**: on step edge, sample a deterministic PRNG (matching DrumSequencer's approach at `drum_sequencer_core.cpp:404–417`). If `sample > prob_N`, mute the step for this cycle. PRNG state seeded per-operator-instance so patterns are reproducible.

**Editor**: horizontal bar 0..1 per cell. Digit entry `0..9` → `digit/9`.

**Why defer from v1**: Cthulhu doesn't have a pure probability lane (it has Rand Sel, which is different — see B3). But "every step rolls a die" is a well-understood modern-arp feature. Simple to ship.

**Priority**: Medium-high. The most-commonly-requested generative feature after gate.

### A3. Per-step Late (±0.5 step microtiming)

**Param**: `late_N` (float -0.5..+0.5, default 0.0). Positive = fire late; negative = early.

**Engine**: step trigger fires at `step_start + late_N * step_duration` instead of `step_start`. Requires peeking ahead to the next step's `late_N` when evaluating gate closure. Moderate complexity — the cleanest implementation defers the trigger into the *next* block if `late_N > 0`, and anticipates into the *current* block if `late_N < 0`.

**Editor**: horizontal centered bipolar bar per cell. Tiny (most steps stay at 0).

**Why defer from v1**: compute() changes are non-trivial (the trigger scheduling moves from "step boundary" to "step boundary ± late") and need their own test coverage. Also interacts with global `swing`. Land after someone asks for it.

**Priority**: Medium. Huge for shuffle-feel authoring, negligible for straight patterns.

## Tier B — new semantic dimensions (single-output)

These go beyond mechanical modifiers — they change *what* the arp plays, still within a single-note-per-step output model.

### B1. Shape-based Note Override values

**Current Note Override**: `0` (follow global), `1..8` (pool index), `9` (mute). **Cthulhu adds**: `up`, `down`, `up/down`, `down/up`, `fingered-top` (every 2nd note is high), `fingered-bottom` (every 2nd note is low), `down-then-up`, `up-then-down`.

**Param**: extend `note_override_N` range to 0..17 (or introduce a second param `note_shape_N`). Values 10..17 map to shape indices; the existing `vivid_sequencers::arp_pattern_index` helper already knows how to resolve shape + step to a pool index.

**Engine**: small switch — if `note_override_N >= 10`, resolve shape index → `arp_pattern_index(shape, raw_step, pool_count)`. Otherwise current behavior.

**Editor**: Note Override cell renders a mini glyph for each shape (Cthulhu has icons for these). Click cycles or digit entry.

**Why defer from v1**: the v1 Note Override already handles "specific pool index" + "mute", which covers the most-requested authoring pattern. Shape-per-step is the *next* tier of authoring power — let it land once users start asking "can I do an up-and-down *only* on step 3?"

**Priority**: Medium. High UX payoff but no rush while the 1..8 / mute vocabulary is still being exercised.

### B2. Position Reset marker

**Param**: `reset_N` (bool, default false) — when a step fires with `reset_N == true`, force the arp's internal step counter to restart from step 1 *at this step*.

**Engine**: one check in `compute()`: if `reset_N && step_changed`, set `step_offset_` so `raw_step` becomes 0. Subtle — needs to not confuse the `prev_had_notes_` reset logic.

**Editor**: small down-arrow overlay glyph on any step. Alt-click to toggle (matches Cthulhu's gesture).

**Why defer from v1**: useful but narrow — lets users build longer "intros" followed by a looping body. Currently users can achieve similar effects by repeating the pattern body.

**Priority**: Low-medium. Nice polish; ship alongside shape-based Note Override.

### B3. Rand Sel — per-step Note Override deviation

Cthulhu's Rand Sel lane isn't binary probability. It's a **signed deviation amount**: halfway = no change, above halfway = output larger pool index than assigned, below = smaller. Intensity scales with distance from halfway.

**Param**: `rand_sel_N` (float 0..1, default 0.5).

**Engine**: after resolving the Note Override's target pool index, perturb by `(rand_sel_N - 0.5) * some_scale * rng_offset`. If above halfway, bias toward larger indices; below, toward smaller.

**Editor**: bipolar bar like Transpose but 0..1 with 0.5 center.

**Why defer from v1**: it's a subtle musical tool — straight A2 probability covers "sometimes silent," which is what 90% of users want. Rand Sel covers "sometimes play a different chord note," which is more advanced. Ship after B1 so the Note Override vocabulary is already rich enough to reward deviation from it.

**Priority**: Low. Niche feature; ship with B1/B2 as a bundle.

### B4. Pitch lane with scale-degree enables

Cthulhu's Pitch tab: per-step semitone transpose, but only applied to chord **scale degrees** you've enabled (seven toggle buttons, one per degree). E.g. enable only degree "3" and per-step Pitch = +1 → the third of every chord gets bumped a semitone, root and fifth unchanged.

**Params**: `pitch_N` (int -12..+12), plus 7 global `pitch_degree_enable_N` bools.

**Engine**: requires chord-root analysis on the input pool. We don't have this. Minimum: use the sorted pool (already done for non-Order modes) and treat index mod 7 as the scale degree. That's a rough approximation — Cthulhu does a "Hindemith analysis" per chord to identify root, third, fifth, etc.

**Why defer from v1**: chord-root analysis is a research topic, and the authoring gesture (selecting which degrees are eligible) introduces a new UI idiom we'd only use here. Pair with a broader chord-awareness feature if we ever add one.

**Priority**: Low. Engineering-heavy; distinct feature that deserves its own scoping.

## Tier C — output architecture changes (polyphonic)

These items change the operator's output semantics — from "one note per step" to "multiple notes per step." Significant engine surgery; each deserves its own design pass.

### C1. Harmony lane (per-step second note)

**Cthulhu**: second note per step, ±1 octave relative to the main step output.

**Engine impact**: the operator's output is currently a 1-lane scalar (`note`) + 1-lane MIDI. To emit two notes simultaneously, we need either (a) two separate scalar outputs (`note_a` / `note_b`) with independent gate and velocity, or (b) a 2-voice lane-array output. Option (b) is cleaner but changes every downstream consumer of the arp's `note` output.

**Editor impact**: new lane, but interpretation depends on (a) vs (b).

**Why defer**: requires picking the polyphony approach, and it's the same decision point C2 would need — better to do them together.

**Priority**: Medium-high if users start asking for two-voice arps; low until then.

### C2. Chord Mode

**Cthulhu**: step outputs the **whole chord** transposed so the Note Override's target becomes the lowest note. Classic chord-inversion arpeggios (C major chord with Note=1 → C-E-G; with Note=3 → E-G-C; with Note=5 → G-C-E).

**Engine impact**: same polyphony question as C1, but the lane-count is variable (depends on pool size). Cleanest as a lane-array output.

**Editor impact**: a global toggle ("Chord Mode" on/off) would suffice; no per-step UI needed if it's a mode. Cthulhu makes it a global switch.

**Priority**: Paired with C1. If we build polyphonic output, both C1 and C2 land together.

### C3. Per-lane pattern length + per-lane Clock Div

**Cthulhu**: each of the 8 lanes has its own step count (1..16+) and its own playback-speed divider (1, 2, 4, 8×). Lanes wrap independently. Result: polymetric textures from simple ingredients.

**Engine impact**: each lane needs its own `raw_step` counter. Complicates `compute()` — no single `mod_idx` applies to all lanes. Moderate effort but contained.

**Editor impact**: green length-indicator line on each lane (Cthulhu's UI). Small "Clock Div" number field per lane (1/2/4/8).

**Why defer**: compute complexity + editor complexity compound. And the single-length model in v1 is "good enough" for most arp patterns — polymetric is a power-user feature.

**Priority**: Medium. Real composer-grade feature; worth doing, just not first.

### C4. Multiple pattern slots

**Cthulhu**: 12 slots (A–L), each a full pattern snapshot, switchable in real-time via MIDI or a slot-index param.

**Engine impact**: pattern state (all per-step params) × N slots. Either N × the current param count (expensive) or a single active-pattern set + an off-DSP slot store. DrumSequencer has pattern A/B — a simpler starting point.

**Editor impact**: slot selector (Cthulhu uses a 12-button row labeled A–L). Per-slot rate knob.

**Why defer**: DrumSequencer's A/B pattern already exists as a model; whether Arpeggiator needs 2 or 12 slots is a product question. Probably start with 2 if we do this.

**Priority**: Medium. Nice for live performance; less important for sequenced authoring.

## Tier D — adjacent features

### D1. Note Output Prevention

**Cthulhu**: single-octave piano in the arp section. Each key has 4 states: normal (note passes), mute (blocked), nudge-up (+1 semitone), nudge-down (−1 semitone).

**Engine impact**: post-filter on the MIDI output. After resolving `out_note`, look up `out_note % 12` in a 12-entry state array. Apply mute / nudge as indicated.

**Editor impact**: 12-key piano widget with per-key state cycling on click. Independent UI region (not a lane).

**Why defer**: orthogonal to per-step authoring; can land anytime. Useful for locking arps into a scale.

**Priority**: Medium-high. Simple, self-contained, well-understood.

### D2. Retrigger policy switch

**Current behavior**: the arp resets to step 0 when the input pool transitions from empty to non-empty (`prev_had_notes_` edge). New MIDI notes arriving on an already-active chord do not retrigger.

**Cthulhu's Retrigger switch**: when enabled, every new MIDI note-on resets the pattern to step 1 regardless of whether the chord is already playing.

**Param**: `retrigger_on_new_note` (bool, default false).

**Engine impact**: tracking new-note-on edges through the input lane-array. Small but needs a dedicated edge detector since currently `prev_any_gate_` only tracks gates-down transitions.

**Priority**: Low. Most users don't notice this; when they do, it's obvious what they want.

### D3. Free-rate mode

**Cthulhu 1.1 addendum**: "Arp Free Rate (Hz)" option — arp clock runs at a free Hz rate instead of tempo-synced.

**Param**: `rate_mode` (synced/free), `free_rate_hz` (0.01..50.0).

**Engine impact**: bypass beat-phase tracking when free. Similar to Sequencer's existing rate_mode.

**Priority**: Low. Most arps are tempo-synced. Ship if there's a specific use case.

## Ranking the deferred bundle

If time comes to ship another round of arp features, the recommended order — based on impact vs. engine cost:

1. **A2 Probability** — high value, trivial engine diff.
2. **D1 Note Output Prevention** — high value, self-contained.
3. **A1 Per-step Octave** — low cost, immediate clarity win.
4. **A3 Late (microtiming)** — high expressive payoff, moderate compute.
5. **B1 Shape-based Note Override values** — extends the v1 vocabulary naturally.
6. **C4 Multi-slot patterns** (A/B, mirror DrumSequencer) — live-performance feature.
7. **C3 Per-lane length + Clock Div** — polymetric.
8. **C1/C2 Harmony + Chord Mode** — requires polyphonic output decision.
9. **B2 Position Reset**, **B3 Rand Sel**, **D2 Retrigger**, **D3 Free-rate** — polish items to bundle.
10. **B4 Pitch with scale-degrees** — requires chord-root analysis; big.

## Architectural notes for future work

- **Polyphonic output decision** (C1/C2): when we commit to multi-note-per-step output, prefer a lane-array `notes` output over N parallel scalar outputs. Downstream consumers (DrumKit, synths, etc.) already handle lane arrays; scalar-to-lane-array is a one-time adapter.
- **Rand Sel vs. Probability** (A2 vs. B3): if we ship A2, document clearly that B3 is the *bidirectional* randomness ("deviate from assigned") while A2 is *binary* ("fire or don't"). Keeping both is fine; conflating them is not.
- **Scale-degree awareness** (B4): would unlock several Cthulhu-style features. If we build chord-root analysis once, B4 and smarter Transpose and smarter Octave all benefit.
- **Editor-shared helpers**: as more Cthulhu-style lanes land, the `arpeggiator_editor_shared` module grows. After 5+ lanes, consider whether `operators/shared/editor_ui/` should absorb lane-rendering primitives (vertical fader cell, centered bipolar cell, discrete-state glyph cell).
