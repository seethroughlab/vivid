# MIDI Pattern Editor

## Context

Vivid has no visual way to enter melodies, chords, or rhythmic patterns. The node graph cannot substitute for point-and-click note entry. This is the primary composition gap for the live performance / generative audience: you can build sounds (especially once CLAP hosting lands), but you can't write music without a pattern editor.

The goal is **not** a full DAW piano roll with an infinite timeline. It's a bounded loop editor — an Ableton-clip-style grid for phrases up to 8 bars. Users create a `MidiPattern` node, draw notes visually, and the pattern loops in sync with the graph metronome, feeding downstream instruments (including CLAP instruments).

## The `MidiPattern` Operator

- **Domain:** Control (outputs note events, not audio samples)
- **Inputs:** none (self-contained loop)
- **Outputs:** A note-event port (same wire format as `MidiInput` → synths) — or potentially exposes the same port type used by the existing MIDI infrastructure
- **Params:**
  - `length_bars` (1, 2, 4, 8)
  - `quantize_grid` (1/32, 1/16, 1/8, 1/4)
  - `pattern_data` (TEXT param — serialized note list, stored in graph JSON)
- Pattern loops continuously, BPM-synced to the graph metronome
- Respects Vivid's transport start/stop

### Note Data Format
Each note: `{pitch: 60, start_beat: 0.0, duration_beats: 0.5, velocity: 100}`. Stored as compact JSON in the `pattern_data` TEXT param.

## The Editor UI

`MidiPattern` uses the operator editor system (`EditorWindowManager`) — the same infrastructure as DrumSequencer's custom editor. Opening the editor (Cmd+E or inspector button) launches a dedicated window containing the pattern grid.

### Grid Layout
```
┌─────────────────────────────────────────────────────┐
│  [Length: 2 bars ▼]  [Grid: 1/16 ▼]  [Clear]       │
├──────┬──────────────────────────────────────────────┤
│ C5   │  ██        ██                                │
│ B4   │                                              │
│ A#4  │                  ██                          │
│ A4   │                                              │
│ G#4  │        ██                                    │
│ ...  │                                              │
├──────┴──────────────────────────────────────────────┤
│      │  .  .  .  .  |  .  .  .  .  |  ...          │
│      beat 1          beat 2                         │
└─────────────────────────────────────────────────────┘
```

- Vertical axis: pitch (piano keyboard on left margin, scrollable)
- Horizontal axis: time (beat grid, one column per quantize grid unit)
- Click empty cell → add note at default velocity
- Click+drag right → extend note duration
- Right-click note → delete
- Drag existing note → move pitch/time
- Playhead cursor shows current loop position in real time

### Velocity Editing
Secondary strip below the grid: bar graph per note showing velocity. Click/drag bar to adjust. Default 100; range 1–127.

## Integration with the Graph

```
[MidiPattern] ──note_events──▶ [clap_instrument]
[MidiPattern] ──note_events──▶ [Synth]
[MidiInput]   ──note_events──▶ [Synth]   (existing path, unchanged)
```

The note-event port type is the same wire used by `MidiInput`. Multiple `MidiPattern` nodes can feed one instrument (merged), or each can feed a different instrument.

### Sync with Metronome
The operator advances its internal playhead each audio block based on `current_song_time` from the graph metronome. It wraps at `length_bars * beats_per_bar` and emits note-on/note-off events at the correct sample offsets within each block.

## Phased Delivery

### Phase 1 — Operator + basic editor
- `MidiPattern` operator: loops note data, outputs note events, synced to metronome
- Basic editor window: click to add/remove notes, fixed velocity
- `pattern_data` persists with graph JSON
- Test: `MidiPattern` → native synth (no CLAP needed yet, but works with it)

**Done when:** A user can open a `MidiPattern` editor, draw a 4-bar melody, connect it to a synth, hear it loop, save and reload the graph.

### Phase 2 — Polish
- Drag to resize/move notes
- Velocity strip
- Quantize grid selector
- Loop length selector (1/2/4/8 bars)
- Keyboard shortcut reference in editor header

### Phase 3 — Multi-pattern per node (stretch goal)
- Multiple named patterns stored in one node (A, B, C...)
- A `pattern_index` control input selects which plays
- Enables clip-launcher-style switching without multiple nodes

## Key Integration Points

| Concern | Where |
|---|---|
| Operator editor ABI | `src/operator_api/operator.h` — optional editor exports |
| EditorWindowManager | `src/runtime/core/editor_window_manager.{h,cpp}` |
| Graph metronome / song time | `src/runtime/graph/` — `sample_live_metronome()` |
| Note wire format | Match `MidiInput` output port type; check `operators/control/midi_input/` |
| TEXT param persistence | Already supported; `pattern_data` is a large TEXT param |
| Renderer2D drawing | `src/ui/rendering/renderer_2d.{h,cpp}` — used for editor grid |
| VividDrawAPI | `src/operator_api/draw_api.h` — operator-safe drawing surface |

## Key Risks

| Risk | Mitigation |
|---|---|
| Note events at exact sample offsets | Must compute sample-accurate note-on/off within each block, not just "fire at block start" |
| Editor ↔ audio thread sync | Pattern data must be double-buffered or locked; audio thread reads, UI thread writes |
| Large patterns in TEXT param | 8 bars of dense 1/32 notes is ~256 notes — JSON is ~15KB, fine |
| Transport stop/restart | Notes in flight when transport stops must emit note-off immediately |
