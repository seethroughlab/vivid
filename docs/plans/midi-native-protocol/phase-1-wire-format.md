# Phase 1 — Native note protocol + emitter/synth migration

**Status**: planned. Introduces native note IDs and note-driven synth internals, but does not remove any graph-visible polyphonic control surfaces yet.

**See also**: [README.md](README.md) for cross-cutting decisions and overall context.

## Goal

Replace the internal MIDI-shaped custom ref with a native note transport that gives every active note a stable `note_id`, then migrate emitters and voice synth internals to use that identity directly.

This phase is intentionally narrow:

- it defines the internal note/event contract
- it migrates emitters to allocate and preserve `note_id`s
- it migrates synth internals to key voice state by `note_id`
- it does **not** delete `VoiceAllocator`
- it does **not** delete synth lane-note inputs
- it does **not** claim tracker expression authoring is solved

## Native transport contract

Replace the current internal transport with a native note type, e.g.:

```c
typedef enum VividNoteEventType {
    VIVID_NOTE_ON,
    VIVID_NOTE_OFF,
    VIVID_NOTE_PITCH_BEND,
    VIVID_NOTE_PRESSURE,
    VIVID_NOTE_TIMBRE,
} VividNoteEventType;

typedef struct VividNoteEvent {
    uint8_t  type;
    uint8_t  note_number;
    uint8_t  velocity_or_reserved;
    uint8_t  reserved;
    uint32_t frame_offset_samples;
    uint64_t note_id;      // non-zero for every per-note event
    float    value;        // bend semis, pressure 0..1, timbre 0..1, or unused
} VividNoteEvent;
```

```c
typedef struct VividNoteBuffer {
    VividNoteEvent events[VIVID_NOTE_BUFFER_CAPACITY];
    uint32_t       count;
} VividNoteBuffer;
```

Lock these semantics:

- `note_on`, `note_off`, `per_note_pitch_bend`, `per_note_pressure`, and `per_note_timbre` are the Phase 1 event types
- every per-note event carries a non-zero `note_id`
- `note_id` is allocated by the emitter at note-on and reused for all follow-up events and the matching note-off
- re-triggering the same pitch creates a fresh `note_id`
- public graph-facing port renames to `notes_in` / `notes_out` should be done as one coordinated cut; do not add a temporary port-alias system just for this migration

## Emitter migration

Update all note emitters so they output the native note buffer and preserve `note_id` lifecycle explicitly.

Core requirements:

- every note-on allocates a fresh `note_id`
- every note-off reuses the original `note_id`
- emitters that transform note structure (`Arpeggiator`, pattern tools, etc.) allocate their own new ids unless they are intentionally forwarding an unchanged note identity

Primary emitters in scope:

- Tracker
- NotePattern
- ChordProgression
- Sequencer
- PatternSeq
- Arpeggiator
- Euclidean
- DrumSequencer
- PhaseToMidi / other note emitters
- `MidiInput`
- `MidiFilePlayer`

Boundary-specific guidance:

- `MidiInput` becomes a native-note emitter by synthesizing `note_id`s from incoming external note streams and converting MPE expression into the native per-note event types
- `MidiFilePlayer` becomes a native-note emitter using the same note-id synthesis rules
- MPE parsing belongs at the boundary, but Phase 1 should evolve existing `MidiInput` rather than introducing an overlapping second path immediately
- internal helper code can stage the migration, but the public operator surface should not rely on a runtime port-rename alias layer that does not exist today

## Synth/internal consumer migration

Update voice synth internals so their active-voice bookkeeping is keyed by `note_id` instead of note number or MIDI channel assumptions.

Lock these implementation expectations:

- synth voice allocators keep `note_id` in each active slot/state record
- note-off and per-note expression lookup is by `note_id`
- same-pitch overlapping notes are treated as distinct voices
- per-note pitch bend, pressure, and timbre mutate only the matching active voice

Voice synths in scope:

- `FmSynth`, `Sampler`, `SP404`, `Slicer`
- wavetable package voice synths (`AnalogOsc`, `WavetableOsc`, `WavetableLayer`, `NoiseLayer`, `SubOsc`)

Lane-driven graph surfaces remain untouched in this phase:

- lane-note synth inputs stay
- `VoiceAllocator` stays
- allocator-era graphs should still work until later phases

## Tests and acceptance criteria

Required test coverage:

- stable `note_id` lifecycle from note-on through note-off
- overlapping same-pitch retriggers allocate distinct ids
- per-note pitch bend routes to the correct active voice
- per-note pressure routes to the correct active voice
- per-note timbre routes to the correct active voice
- two synths consuming the same native note stream remain coherent across retriggers and expression updates
- `MidiInput` and `MidiFilePlayer` emit native-note buffers rather than only basic note-on/off MIDI semantics

Manual sanity:

1. `Tracker -> synth -> audio_out` still plays via the new native note transport.
2. `MidiInput` receiving MPE input produces correct per-note bend/pressure/timbre events.
3. Two simultaneous same-pitch retriggers produce two active synth voices rather than collapsing to one.

## Risks

- **Wire migration cost.** Renaming the internal contract from MIDI-shaped buffers to native notes touches many emitters and synths at once.
- **Boundary confusion during rollout.** The docs and operator descriptions must be explicit that external MIDI is still supported even though the internal graph contract is no longer MIDI-shaped.
- **Tracker expectations.** Tracker does not get credit for expression authoring in this phase unless it actually emits per-note expression events; note-on/note-off alone are not sufficient.

## Out of scope for Phase 1

- Deleting `VoiceAllocator`
- Deleting synth lane-note inputs
- Standardized synth breakout outputs
- Preset/demo migration
- Tracker expression authoring UI
