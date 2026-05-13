# MIDI Out Operator

## Context

Vivid can receive MIDI from external sources via `MidiInput`, but has no path in the other direction. `RtMidiOut` is already available in `deps/rtmidi/` — it just has never been instantiated or wired to an operator. This operator closes that gap: it accepts the same note-event wire format that all internal synths and `clap_instrument` consume, and serializes those events as MIDI bytes to a selected system output port.

## The `midi_out` Operator

- **Domain:** Audio (note events flow on the audio thread; sending must happen there for timing accuracy)
- **Inputs:** `notes_in` — note-event port (same wire format as `MidiInput` → synths)
- **Outputs:** none (pure sink)
- **Params:**
  - `device` (string) — system MIDI output port name; empty = first available; `"virtual"` = open a named virtual port
  - `virtual_port_name` (string) — name for the virtual port when `device = "virtual"` (default: `"Vivid"`)
  - `channel` (int, 1–16) — MIDI channel to send on (default: 1)
  - `velocity_scale` (float, 0–2) — multiply incoming velocity before sending; useful for level matching

### Note Event Translation

| Vivid event | MIDI bytes |
|-------------|-----------|
| note-on | `[0x90 + (channel-1), pitch, velocity]` |
| note-off | `[0x80 + (channel-1), pitch, 0]` |
| pitch bend | `[0xE0 + (channel-1), lsb, msb]` (14-bit, mapped from −1..+1) |
| aftertouch (per-note) | `[0xA0 + (channel-1), pitch, pressure]` |
| CC (mod wheel, expression) | `[0xB0 + (channel-1), cc_num, value]` |

Poly-mode events (MPE) are translated naively to channel-mode MIDI: all voices share one channel. Full MPE-out (one channel per voice) is a stretch goal.

## Architecture

Each `midi_out` operator instance owns a single `RtMidiOut` object. This mirrors how `CLAPPluginInstance` owns its plugin handle — the operator is responsible for its lifetime.

```
AudioProcessable::process_audio()
    for each note event in notes_in buffer:
        serialize to MIDI bytes
        rtmidi_out->sendMessage(bytes)
```

Port enumeration for the `device` param picker runs on the main thread at `probe()` time and populates a string-list param with discovered port names. The audio thread opens the selected port at `init()` and closes at `deinit()`.

**Thread note:** `RtMidiOut::sendMessage()` is safe to call from the audio thread on macOS (it posts to CoreMIDI's internal queue). No locking needed.

**Note-off on stop:** When the graph stops or the node is removed, in-flight notes must be released. The operator sends a MIDI All Notes Off (CC 123, value 0) to the selected channel during `deinit()`.

## Location

`operators/audio/midi_out/`

Follows the same dylib operator conventions as other audio operators. Add to `cmake/operators.cmake`.

## Key Integration Points

| Concern | Where |
|---------|-------|
| RtMidi dependency | `deps/rtmidi/` — already vendored, already used by `SystemMidiListener` |
| `SystemMidiListener` (input reference) | `src/runtime/audio/system_midi.{h,cpp}` — mirror its port-open pattern for output |
| AudioProcessable interface | `src/operator_api/audio_operator.h` |
| Note-event wire format | `operators/control/midi_input/` — match its output port type exactly |
| Operator CMake registration | `cmake/operators.cmake` |
| MCP tools | `src/cli/mcp_server.cpp` — no new tools needed; existing `set_param` handles device/channel |

## Phased Delivery

### Phase 1 — Core note output
- `midi_out` operator: opens a system MIDI port, translates note-on/note-off, sends on audio thread
- `device` param with port name picker (discovered at probe time)
- `channel` param
- All Notes Off on deinit / graph stop
- Test: `MidiPattern` → `midi_out` → hardware synth (or DAW via IAC Bus) plays correctly

**Done when:** Notes from any Vivid source reach an external MIDI destination in time and pitch.

### Phase 2 — Expression and velocity
- Pitch bend, aftertouch, CC (mod wheel, expression) translation
- `velocity_scale` param
- `virtual_port_name` param + virtual port mode

**Done when:** Full expressive data reaches external targets, and Vivid appears as a named MIDI source in other apps.

## Key Risks

| Risk | Mitigation |
|------|-----------|
| Port open/close on device change at runtime | Reopen port on `device` param change; handle gracefully if port disappears |
| Multiple `midi_out` nodes targeting the same port | Each owns its own `RtMidiOut` instance; CoreMIDI handles contention |
| Note-off missing on graph reload (notes stuck) | Send All Notes Off on every `deinit()` call, not just on explicit stop |
| Timing jitter | `sendMessage()` posts to CoreMIDI — latency is CoreMIDI's responsibility; acceptable for most use cases |
