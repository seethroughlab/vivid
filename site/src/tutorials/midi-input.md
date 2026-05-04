## What you'll build

A polyphonic synthesizer playable from a MIDI keyboard or controller — MidiInput translating hardware note data into Vivid's internal note stream, driving FmSynth.

## Operators used

| Operator | Domain | Role |
|----------|--------|------|
| MidiInput | Control | Translates hardware MIDI into a notes_out lane stream |
| FmSynth | Audio | Two-operator FM synthesis with per-voice ADSR |
| Gain | Audio | Master volume |
| audio_out | — | Audio output |

## Step 1: Connect your MIDI device

Make sure your MIDI device is connected and recognized by macOS before adding MidiInput.

Add **MidiInput**. In the inspector:
- `channel` — set to **0** for omni (receive all channels), or a specific channel (1–16)
- `mode` — **poly** for polyphonic play (multiple simultaneous notes)

MidiInput does not produce audio — it produces a `notes_out` lane stream, which is Vivid's internal note format used by all synthesizers.

## Step 2: Add FmSynth

Add **FmSynth**. Connect `midi1/notes_out` → `fm1/notes_in`.

Connect `fm1/output` → `audio_out/input`.

Play a note on your keyboard. You should hear the FM synth respond.

## Step 3: Add Gain for volume control

Add **Gain** between FmSynth and the output. Connect `fm1/output` → `gain1/input`, `gain1/output` → `audio_out/input`. Set `gain` to **0.7**.

## Step 4: Tune the FM synthesis

FmSynth uses classic two-operator FM: a carrier oscillator modulated by a modulator oscillator.

**Core params:**
- `mod_ratio` — frequency ratio of modulator to carrier. Integer ratios (1, 2, 3) produce harmonic timbres; non-integer ratios produce inharmonic, bell-like tones
- `mod_index` — depth of frequency modulation. Low values (0–1) = subtle FM coloring; high values (5–20) = bright, complex harmonics
- `carrier_freq` — base carrier frequency (overridden by MIDI note pitch)

**ADSR envelope:**
- `attack` — time to reach full amplitude after note-on
- `decay` — time to fall from peak to sustain level
- `sustain` — amplitude while note is held
- `release` — time to fade after note-off

## Step 5: Add reverb

Insert **Reverb** between Gain and the output for a natural space. Set `mix` to **0.25** for subtle room ambience.

## What's happening

```
MIDI device → MidiInput → notes_out (lane stream)
                                   ↓
                              FmSynth/notes_in
                                   ↓
                           voices_out (polyphonic audio)
                                   ↓
                               Gain → audio_out
```

`notes_out` is a lane-bearing stream: each active note is a lane, carrying pitch, velocity, gate, and pressure data. FmSynth spawns a voice per lane, mixing them down into a single stereo audio buffer via `voices_out`.

The polyphony limit is set by FmSynth's voice count. Voices are allocated on note-on and released on note-off after the release envelope completes.

## Tip: Use MidiInput outputs for modulation

MidiInput also exposes individual scalar outputs: `velocity`, `gate`, `mod_wheel`, `pitch_bend`, `cc_value`. These are Control-domain values and can wire to any parameter.

For example: `midi1/mod_wheel` → `fm1/mod_index_cv` maps the modwheel to FM depth in real time.

## Next steps

- [Multi-Voice Patterns with Lanes](../multi-voice-lanes/) — generate note patterns without a keyboard
- [Control and Modulation](../control-and-modulation/) — use MidiInput outputs as modulation sources
