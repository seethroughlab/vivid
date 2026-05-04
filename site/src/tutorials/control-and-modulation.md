## What is the Control domain?

Control operators produce scalar values — numbers that change over time, running at frame rate (~60 Hz) on the main thread. They're Vivid's modulation layer.

A Control output (orange wire) can connect to **any numeric parameter** on any operator, regardless of domain — GPU, Audio, or another Control operator. This is how you animate parameters without writing code.

## Core Control operators

| Operator | What it produces |
|----------|-----------------|
| ClockFr | Beat phase (0–1 sawtooth), beat trigger impulse |
| LfoFr | Periodic oscillation at a set frequency or beat division |
| Smooth | Exponential smoother with independent rise and fall times |
| Envelope | ADSR shape triggered by a gate signal |
| Math | Arithmetic on control signals: add, multiply, remap |

## Exercise 1: LFO driving a parameter

The simplest modulation: an LfoFr driving a single parameter.

1. Add **Metaball** → `video_out`
2. Add **LfoFr**. Connect `lfo1/value` → `metaball/pos_x`

The Metaball sweeps left and right at 1 Hz.

**Key LfoFr params:**
- `frequency` — oscillation rate in Hz (or beat division in sync mode)
- `amplitude` — output range is `±amplitude` around the center
- `offset` — shifts the center point
- `waveform` — sine, saw, square, triangle, random
- `polarity` — bipolar (−1 to +1) or unipolar (0 to 1)

## Exercise 2: Sync LFO to a tempo

LfoFr can lock to a beat clock instead of running freely.

1. Add **ClockFr**. Set `bpm` to **120**.
2. Connect `clock1/beat_phase` → `lfo1/beat_phase`
3. On LfoFr, set `rate_mode` to **sync**

The LFO now cycles exactly once per beat at 120 BPM. Change `sync_division` to make it cycle every 2 beats, every bar, etc.

ClockFr also outputs `beat_trigger` — an impulse at each beat boundary. This is useful for triggering one-shot events rather than continuous oscillation.

## Exercise 3: Smooth for slew and envelope following

Smooth applies exponential lag to a signal, with independent time constants for rising and falling edges.

1. Connect any fast-changing signal (e.g., LfoFr with square wave) to `smooth1/input`
2. Connect `smooth1/value` → a parameter

**Key Smooth params:**
- `rise_time` (seconds) — how long to slew up to a new higher value
- `fall_time` (seconds) — how long to decay to a new lower value

Set `rise_time = 0.005` and `fall_time = 0.4` to get a fast attack / slow release shape — the standard envelope-follower feel used in audio-reactive visuals.

## Exercise 4: One LFO, two domains

Control signals are domain-agnostic. The same LfoFr output can wire into an audio parameter and a GPU parameter simultaneously.

1. Start with **Oscillator** → **Gain** → `audio_out`
2. Start with **Metaball** → `video_out`
3. Add **LfoFr**
4. Connect `lfo1/value` → `osc1/frequency` (audio parameter — hear the pitch wobble)
5. Connect `lfo1/value` → `metaball/pos_x` (GPU parameter — see it move)

Both audio and GPU are now locked to the same modulation source. This is the foundation of synchronized AV performance.

## What's happening

```
ClockFr → LfoFr/beat_phase     (sync to tempo)
LfoFr/value → Oscillator/freq  (audio modulation)
LfoFr/value → Metaball/pos_x   (GPU modulation)
```

The Control domain runs at frame cadence and delivers scalar values on every frame tick. When a Control output connects to an audio-domain parameter, the value is sampled once per audio buffer (bridged via the `AudioFrameBridge`). The audio engine interpolates smoothly between frames, so no zipper noise occurs.

## Next steps

- [Audio-Reactive Visuals](../audio-reactive-visuals/) — replace LFO with audio analysis as the modulation source
- [Using MIDI Input](../midi-input/) — use hardware controllers as modulation sources
