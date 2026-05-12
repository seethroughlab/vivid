# MIDI Clock Output

## Context

Vivid maintains an internal metronome (BPM, beat phase, bar phase) that all operators and CLAP plugins can read. But this clock is invisible to external apps. A standalone app like EZdrummer 3 has no way to lock its tempo to Vivid's — you'd have to set both manually.

MIDI Clock (MIDI spec §5) is the standard protocol for syncing external devices. It sends 24 timing pulses per quarter note (24 PPQ) at a rate derived from the current BPM, plus transport messages (Start/Stop/Continue). Any MIDI Clock–capable device can lock to it.

## The `midi_clock_out` Operator

- **Domain:** Audio (clock pulses must be computed at audio-block cadence for timing accuracy)
- **Inputs:** none (reads metronome state from audio context)
- **Outputs:** none (pure sink — writes MIDI bytes to a system port)
- **Params:**
  - `device` (string) — system MIDI output port name; same semantics as `midi_out`
  - `enabled` (bool) — start/stop clock broadcast without removing the node
  - `send_transport` (bool, default: true) — send MIDI Start (0xFA) / Stop (0xFC) / Continue (0xFB) in sync with Vivid's transport
  - `song_position` (bool, default: true) — send Song Position Pointer (0xF2) on Start so receivers know which beat they're starting at

### Timing Model

MIDI Clock requires 24 pulses per quarter note. At a given BPM:

```
clock_interval_samples = sample_rate × 60 / (BPM × 24)
```

Example: at 120 BPM, 48000 Hz sample rate → one clock every **1000 samples** (about every 2 audio buffers at 512-sample block size).

The operator maintains a fractional sample accumulator. Each audio block, it advances by `block_size` samples and fires a clock pulse (byte `0xF8`) for each whole interval crossed, using `sendMessage()` at the correct intra-block moment. This gives sub-millisecond timing accuracy — comparable to a hardware MIDI sequencer.

```
process_audio():
    accumulator += block_size
    interval = sample_rate × 60 / (bpm × 24)
    while accumulator >= interval:
        send MIDI Clock (0xF8)
        accumulator -= interval
```

The BPM is read from `metronome_bpm` in the audio context each block, so tempo changes take effect immediately.

## Transport Messages

When `send_transport = true`:

| Vivid event | MIDI bytes sent |
|-------------|----------------|
| Transport starts from position 0 | Song Position Pointer (if enabled) + Start (0xFA) |
| Transport starts mid-song | Song Position Pointer + Continue (0xFB) |
| Transport stops | Stop (0xFC) + All Sound Off (CC 120) on channel 1 |

Song Position Pointer encodes the current beat position in MIDI beats (1/16th notes), letting external sequencers start from the correct position rather than always rewinding to bar 1.

## Architecture

Like `midi_out`, each `midi_clock_out` instance owns a single `RtMidiOut` object. Port lifecycle (open at `init`, close + send Stop at `deinit`) mirrors `midi_out`.

The operator does **not** depend on `midi_out` — they are independent. A user can run one without the other. Both can target the same MIDI port (CoreMIDI merges concurrent senders), though it is simpler to target the same device from both if they are used together.

## Location

`operators/audio/midi_clock_out/`

## Key Integration Points

| Concern | Where |
|---------|-------|
| RtMidi dependency | `deps/rtmidi/` — same as `midi_out` |
| Metronome BPM in audio context | `src/operator_api/audio_operator.h` — `metronome_bpm`, `current_song_time` available per block |
| Transport start/stop events | `src/runtime/graph/` — how transport state changes propagate to audio operators; check `AudioProcessable` context fields |
| `midi_out` (companion operator) | `docs/plans/midi-external-routing/midi-out-operator.md` — share port-open pattern |
| Operator CMake registration | `cmake/operators.cmake` |

## Phased Delivery

### Phase 1 — Clock pulses
- `midi_clock_out` operator: opens port, maintains sample accumulator, sends 0xF8 at correct intervals
- `device` and `enabled` params
- Test: connect to DAW via IAC Bus; DAW tempo display locks to Vivid's BPM, including live changes

**Done when:** An external MIDI Clock–capable device follows Vivid's BPM, including tempo changes.

### Phase 2 — Transport sync
- `send_transport` param: send Start/Stop/Continue in sync with Vivid's transport
- `song_position` param: include Song Position Pointer on start
- Test: starting/stopping Vivid transport starts/stops an external sequencer in sync

**Done when:** An external device starts and stops with Vivid's transport and lands on the correct beat.

## Key Risks

| Risk | Mitigation |
|------|-----------|
| BPM changes cause accumulator drift | Recompute interval each block from live `metronome_bpm`; accumulator is self-correcting |
| Two operators (midi_clock_out + midi_out) on same port | CoreMIDI handles this; document that it's fine |
| Accumulator wrap at tempo change mid-block | Cap accumulator at `interval - 1` after a tempo change to avoid spurious burst of clocks |
| Transport state not exposed on audio thread | May need to check whether `transport_running` is available in `AudioProcessable` context; add if not |
