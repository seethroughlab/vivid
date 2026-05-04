## What you'll build

A complete audio-visual performance patch: a drum machine with reverb and delay, driving an animated visual that pulses with the kick and shimmers with the hi-hat. This tutorial assumes you've worked through the earlier guides.

## Graph overview

```
ClockFr ─────────────────────────────────────────────────┐
    │                                                     │
    ├── beat_phase → DrumSequencer                        │
    │                   │                                 │
    │           track_1 → DrumKick ──┐                   │
    │           track_2 → DrumSnare──┤                   │
    │           track_3 → DrumHiHat─┤                   │
    │                                ↓                   │
    │                             Mixer → Reverb → audio_out
    │                                │
    │                          AudioAnalysis
    │                          rms ─→ Smooth(kick) ─→ Metaball/radii
    │                          peak → Smooth(hat)  ─→ LfoFr/amplitude
    │                                                     │
    └── beat_phase → LfoFr ──────────────────────────────→ Metaball/pos_x
                                                          │
                                                    Bloom → Feedback → video_out
```

## Step 1: Drum machine

1. Add **ClockFr** (bpm: 120)
2. Add **DrumSequencer**. Connect `clock1/beat_phase` → `drumseq1/beat_phase`.
   Program: kick on beats 1 and 3, snare on 2 and 4, hi-hat on every eighth note.
3. Add **DrumKick**, **DrumSnare**, **DrumHiHat**. Connect the sequencer track outputs to trigger inputs.
4. Add **Mixer**. Connect all drum outputs to mixer inputs.
5. Add **Reverb** (room_size: 0.4, mix: 0.2). Chain: `mixer1/output` → `reverb1/input` → `audio_out/input`.

Hear the drum pattern.

## Step 2: Audio analysis

Insert **AudioAnalysis** inline before the reverb:
- `mixer1/output` → `analysis1/input` → `reverb1/input`

The audio still plays unchanged — AudioAnalysis is a passthrough.

## Step 3: Build the visual

1. Add **Metaball** (count: 3). Connect `metaball1/texture` → `video_out/input`.
2. Add **Bloom** and **Feedback**. Chain: `metaball1/texture` → `bloom1/input` → `feedback1/input` → `video_out/input`.

A static cluster of blobs appears with glow and trails.

## Step 4: Animate the blobs

Sync the blob motion to the clock:
1. Add **LfoFr**. Connect `clock1/beat_phase` → `lfo1/beat_phase`. Set `rate_mode` to **sync**, `waveform` to **sine**.
2. Connect `lfo1/value` → `metaball1/pos_x`.

The blobs sweep horizontally, locked to tempo.

## Step 5: Make the blobs react to kick

1. Add **Smooth** (rise_time: 0.005, fall_time: 0.4). Connect `analysis1/rms` → `smooth1/input`.
2. Connect `smooth1/value` → `metaball1/radii`.

Set a wire remap on `smooth1/value → metaball1/radii`: to_min: 0.05, to_max: 0.4.

The blobs pulse outward on each kick hit.

## Step 6: Make hi-hat affect motion intensity

Route the audio peak into the LFO amplitude so the hi-hat adds shimmer:
1. Add a second **Smooth** (rise_time: 0.002, fall_time: 0.15).
2. Connect `analysis1/peak` → `smooth2/input`.
3. Connect `smooth2/value` → `lfo1/amplitude`.

Now the hi-hat energy briefly widens the sweep range of the blobs.

## Step 7: Final tuning

| What to adjust | Where |
|----------------|-------|
| Overall tempo | ClockFr `bpm` |
| Kick punch | DrumKick `pitch_env`, `click` |
| Reverb space | Reverb `room_size`, `damping` |
| Trail length | Feedback `decay` |
| Glow intensity | Bloom `threshold`, `passes` |
| Blob density | Metaball `count`, `threshold` |
| Motion range | LfoFr `amplitude`, Smooth `fall_time` |

## What's happening

This patch demonstrates all three Vivid domains working together:

- **Control**: ClockFr drives LfoFr and DrumSequencer at tempo. Smooth shapes audio scalars into visual-ready signals.
- **Audio**: DrumSequencer triggers drum voices, mixed and processed at sample rate.
- **GPU**: Metaball + effects chain renders at frame rate, driven by Control outputs.

The cross-domain bridges are:
1. `analysis1/rms` (Audio scalar → Control scalar via AudioFrameBridge)
2. `lfo1/value` and `smooth/value` (Control scalar → GPU parameter)

Neither Audio nor GPU ever communicate directly. Everything routes through Control.

## Performance tips

- Use **Macros** to surface the key parameters (BPM, kick level, visual brightness) as a single performance panel
- Save **Variations** (`Cmd+S` in the variations panel) to capture different states of the patch for live switching
- Try replacing DrumSequencer with **Euclidean** on one track for generative rhythmic variation

## Next steps

- [Writing a Custom GPU Operator](../custom-gpu-operator/) — build a bespoke visual effect for this patch
- [Using MIDI Input](../midi-input/) — add a melodic synth layer driven by keyboard
