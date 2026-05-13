# MIDI External Routing

## Context

Vivid has full MIDI input (via `MidiInput` operator + `SystemMidiListener`) but no MIDI output. This means Vivid cannot drive external hardware synths, standalone drum machines, or any MIDI-capable app — you can only receive from them.

The two gaps are independent but complementary:

1. **[MIDI Out Operator](midi-out-operator.md)** — Send note events from Vivid's graph to a system MIDI port. Enables driving external instruments (hardware synths, standalone apps like EZdrummer 3) directly from a Vivid graph, using the same note-event wire format that feeds internal synths and CLAP instruments.

2. **[MIDI Clock Output](midi-clock-output.md)** — Broadcast MIDI timing clock (24 PPQ) from Vivid's internal metronome to external devices. Lets external apps and hardware lock their tempo to Vivid's BPM, including Start/Stop/Continue transport messages.

## Infrastructure Note

`RtMidi` is already vendored at `deps/rtmidi/`. The input side (`RtMidiIn`) is fully wired through `SystemMidiListener`. The output side (`RtMidiOut`) exists in the same library but has never been instantiated. Both features build directly on this existing dependency — no new libraries required.

## How They Interact

Used together, these two operators let Vivid act as the tempo master for a live setup: MIDI Clock drives the external app's BPM, and MIDI Out sends patterns or notes to trigger its sounds. Each can also be used independently.

```
[MidiPattern] ──notes──▶ [midi_out] ──────────────────▶ EZdrummer 3 (standalone)
[midi_clock_out] ──────────────────── MIDI Clock ──────▶ EZdrummer 3 (standalone)
```
