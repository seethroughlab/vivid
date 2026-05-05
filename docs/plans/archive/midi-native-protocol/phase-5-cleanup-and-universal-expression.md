# Phase 5 — Cleanup + universal expression coverage

**Status**: complete. Phases 1–4 delivered the migration; Phase 5 closes the seams those phases intentionally left for follow-up.

**See also**: [README.md](README.md) for the migration overview.

## Why Phase 5 existed

Phase 4's audit revealed that the four-phase migration delivered everything the plan called for, but the migration intentionally left several follow-ups for "a later phase." Those follow-ups are now done:

1. **Dead code from the pre-migration era** — `phase_to_midi.h`, `midi_helpers.h`, the `VividMidiBuffer` struct, and the stale `phase_to_midi.json` site doc had zero callers but still compiled and polluted the operator browser.
2. **Two parallel output surfaces on every emitter** — Tracker, MidiInput, NotePattern, ChordProgression, and Arpeggiator all exposed legacy `notes` / `velocities` / `gates` LANE_ARRAY outputs alongside the canonical `notes_out`. MidiInput went further with 8 legacy lane outputs.
3. **Arpeggiator was the last legacy-only consumer** — it only accepted `notes`/`velocities`/`gates` LANE_ARRAY inputs and emitted the same surface as outputs.
4. **Expression was one-synth-deep** — only WavetableLayer (Phase 4 PR3) read `slot.pressure` / `slot.timbre`; the other 8 voice synths set them on note_on but ignored them in their render path.
5. **`vivid::VoiceAllocator<N>` template** still wore the name of the deleted graph operator, confusing readers of `fm_synth.cpp` and similar.
6. **FX_TONE_PORTA dual path** — the legacy `current_pitch` lane broadcast was kept alive in Phase 4 PR4 alongside the new PITCH_BEND emission "for follow-up."

## What shipped (4 PRs)

### PR 1 — Dead code purge + `VoiceAllocator` → `VoiceTable` rename

vivid `80dfdcd1` (26 files, +130/-282) and vivid-wavetable `280517b` (5 files, +10/-10).

- Deleted `operators/shared/sequencer/phase_to_midi.h`, `operators/shared/sequencer/midi_helpers.h`, `src/operator_api/midi_types.h` (the whole file — both `VividMidiBuffer` and `VividMidiMessage` were dead), `site/operators/phase_to_midi.json`, and the dist artifacts.
- Updated `mcp/vivid_opdev_mcp.py` "midi" capability + `mcp/opdev_docs/advanced.md` "MIDI Types" → "Native Note Protocol" so opdev tooling now documents `VividNoteBuffer`/`VividNoteEvent` instead of the dead intermediates.
- Renamed `vivid::VoiceAllocator<N>` → `vivid::VoiceTable<N>` (both repos): `src/operator_api/voice_allocator.h` → `voice_table.h`, plus 5 source-file sweeps in core (`fm_synth`, `note_breakout`, `voice_breakouts.h`, `sampler_common/voice.h`, the test) and 5 in vivid-wavetable (`wavetable_osc_internal.h`, `wavetable_layer_internal.h`, `analog_osc.cpp`, `sub_osc.cpp`, `noise_layer.cpp`). The deleted graph-visible operator is still called `"VoiceAllocator"` in the `test_no_voice_allocator_in_graphs` regression guard — that test asserts JSON strings, not the C++ template name.

### PR 2 — Legacy LANE_ARRAY output removal + FX_TONE_PORTA cleanup

vivid `190cd0f3` (7 files, +280/-491).

- MidiInput: dropped 8 LANE_ARRAY OUTPUT ports (`notes`, `velocities`, `gates`, `pitch_bends`, `pressures`, `slides`, `expressions`, `channels`) and the writer block in `process_frame`. Scalar `aftertouch`/`expression` shifted from `output_values[11]/[12]` to `[8]/[9]`.
- Tracker: dropped 3 LANE_ARRAY OUTPUT ports + writer block; dropped `out_spreads` from `compute()` signature; output_buffers indexing for `row`/`pattern`/`order` shifted from `[3..5]` to `[0..2]`.
- NotePattern: dropped 3 LANE_ARRAY OUTPUT ports, replaced with 3 SCALAR convenience outputs (`note`/`vel`/`gate`) the operator was already populating internally.
- FX_TONE_PORTA: the per-tick PITCH_BEND emission added in Phase 4 PR4 stays as the canonical pitch path; the legacy `current_pitch` lane broadcast that paired with it is gone. The dual-path comment in `process_tick_effects` was updated.
- `tests/audio/test_midi_input_expression.cpp` rewritten end-to-end: replaced `lane_ports[7..18]` reads with `notes_out` event walks (mirrors `tests/operators/test_tracker_expression.cpp`). The CC11 expression and channels-lane assertions had no native equivalent and were removed.
- ChordProgression's matching legacy outputs were intentionally kept through PR2 because Arpeggiator was still consuming them via two checked-in demo graphs — they go away in PR3.

### PR 3 — Arpeggiator native rewrite + ChordProgression legacy lanes drop

vivid `9b335985` (7 files, +345/-176).

- Arpeggiator now consumes a native `VividNoteBuffer` on `notes_in` and emits `NOTE_ON` / `NOTE_OFF` (and bake-forwarded `PRESSURE` / `TIMBRE`) on `notes_out`. Held-set keyed by `uint64 note_id` (NOT MIDI pitch) so MPE same-pitch overlap works. PITCH_BEND on input is ignored (the arp's emitted pitch is the held source's MIDI pitch + per-step transpose). PRESSURE/TIMBRE update the held source's last-known value; on step fire those are emitted as initial expression on the new step's note_id — **snapshot-and-bake**. Live expression updates from the input do NOT propagate (the arp is a step sequencer, not an expression bus). Latch keeps a snapshot when held_count_ drops to 0; a fresh NOTE_ON wave clears the latch.
- Dropped Arpeggiator's 3 LANE_ARRAY input ports and 3 LANE_ARRAY output ports; SCALAR outputs `note`/`vel`/`gate`/`step` moved from descriptor positions `[3..6]` to `[0..3]`.
- Dropped ChordProgression's 3 LANE_ARRAY OUTPUT ports (no remaining consumers); SCALAR `note`/`vel`/`gate` moved from `[3..5]` to `[0..2]`.
- Migrated the two affected demo graphs (`graphs/audio/arpeggiator_metronome_demo.json`, `graphs/audio/sampler_chromatic_demo.json`) to `cp1/notes_out → arp1/notes_in`; the visual `cp1/notes` connection (Math node) became `cp1/note` (scalar first-note convenience output).
- Test harness `ArpHarness::step_at(beat_phase, notes)` now diffs new notes vs. previously-held set and emits NOTE_ON/OFF deltas. 3 new PR3 tests cover held-set semantics (NOTE_OFF removes from rotation), note_id-keyed same-pitch overlap (release one of two same-pitch entries leaves the other held), and snapshot-and-bake expression (held PRESSURE appears on the arp's emitted note_id, not the source's).

### PR 4 — Universal expression coverage + Phase 5 docs

vivid (PR4 commit) and vivid-wavetable (PR4 commit).

Pressure → amplitude is uniform across all 8 voice synths: each gains a `pressure_to_amp` param (range 0..1, default 0.5) and the per-voice render multiplies by `1 + depth × slot.pressure`. At pressure=0 a voice plays at 1× regardless of depth, so a fresh patch with no expression events sounds normal; pressure=1 with depth=0.5 boosts 1.5×.

Timbre maps per synth to the most natural spectral knob:

| Synth | Param | Mapping |
|---|---|---|
| WavetableLayer (Phase 4 PR3 reference) | `timbre_to_position` | offsets wavetable position |
| FmSynth | `timbre_to_mod_index` | scales mod_index per voice — harmonic richness |
| AnalogOsc | `timbre_to_pwm` | offsets pulse width per voice — duty cycle |
| WavetableOsc | `timbre_to_position` | offsets wavetable position (mirrors WavetableLayer) |
| SubOsc | `timbre_to_level` | offsets the sub level — push/dim against the patch |
| NoiseLayer | `timbre_to_tone` | offsets the tone (cutoff) control — open/dark per voice |
| Sampler | `timbre_to_pitch` | per-voice pitch detune in semitones (default ±12 at full timbre = ±1 octave) |
| SP404 | `timbre_to_pitch` | same as Sampler |
| Slicer | `timbre_to_pitch` | same as Sampler |

Drum operators (DrumKick/Snare/Clap/HiHat/Cymbal/Tom) stay opt-out — percussive single-shots have no musical use for pressure/timbre.

`voice_render_frame` in `operators/shared/sampler_common/voice.h` gained two optional parameters (`rate_scale=1.0`, `gain_scale=1.0`) so Sampler/SP404 can apply per-voice expression without each call site implementing pitch/gain shifting. Slicer renders inline (no `voice_render_frame` use) so it wires expression directly into its render block.

## Result

After Phase 5, a newcomer reading any operator sees one obvious surface for each concern:

- **Note routing**: `notes_in` / `notes_out` (`VividNoteBuffer`). No legacy LANE_ARRAY twin.
- **Per-note expression**: the source emits PITCH_BEND/PRESSURE/TIMBRE events on `notes_out`; consumers read `slot.pressure`/`slot.timbre`/`slot.pitch_bend_semis` in their render loop.
- **Per-voice graph composition**: the synth's advanced breakout ports (`voices_out` + `voice_ids`/`voice_gates`/`voice_velocities`/`voice_freqs`), or `NoteBreakout` for control-only fanout that adds the deferred `voice_pitch_bend`/`voice_pressure`/`voice_timbre` lanes.
- **Per-synth voice table**: `vivid::VoiceTable<N>` (internal C++ template; never appears in graph JSON).

Verified across the test suite: vivid's tracker/midi/arpeggiator/synth tests all green; vivid-wavetable's full 22-test suite green. The Phase 4 demo graph (`graphs/audio/tracker_expression_demo.json`) still passes the integration test that asserts pressure swell + timbre sweep are audible.

## Out of scope (future phases or separate efforts)

- **Other emitters gaining native expression authoring** — ChordProgression, NotePattern, MidiFilePlayer can emit notes_out but don't yet author per-note expression. Tracker (Phase 4) and MidiInput (since Phase 1) are the two emitters that author native expression.
- **Live MPE recording** — capturing controller expression into Tracker cells is a separate UI workflow.
- **MIDI 2.0 / MPE export from MidiOutput** — different problem; needs an export-format design.
- **Removing the `frequencies` / `gate` LANE_ARRAY INPUT ports on Filter / DualFilter / Envelope** — these remain the canonical advanced-composition surface, fed by `NoteBreakout/voice_freqs` and `NoteBreakout/voice_gates`.
