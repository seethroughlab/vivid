## What you'll build

Take the oscillator from the previous tutorial and process it through a reverb and delay chain. You'll learn how audio effects insert into a signal path and how to tune wet/dry balance.

## Prerequisites

Complete [Your First Audio Generator](../first-audio-generator/) first, or start with: `Oscillator → Gain → audio_out`.

## Operators used

| Operator | Domain | Role |
|----------|--------|------|
| Reverb | Audio | Algorithmic reverb (room simulation) |
| Delay | Audio | Tape-style delay with feedback |

## Step 1: Insert Reverb

Add **Reverb**. To insert it between Gain and the output:
1. Disconnect `gain1/output` → `audio_out/input`
2. Connect `gain1/output` → `reverb1/input`
3. Connect `reverb1/output` → `audio_out/input`

You should immediately hear the tone in a reverberant space.

**Tune Reverb:**
- `room_size` (0–1, default 0.5) — larger values = longer, more diffuse decay
- `damping` (0–1, default 0.5) — higher values absorb high frequencies, making the reverb darker
- `mix` (0–1, default 0.3) — blend between dry signal and reverb tail

## Step 2: Insert Delay

Add **Delay**. Chain it before the Reverb (delay → reverb is the classic "pre-delay" setup):
1. Disconnect `gain1/output` → `reverb1/input`
2. Connect `gain1/output` → `delay1/input`
3. Connect `delay1/output` → `reverb1/input`

**Tune Delay:**
- `time` (0–2000 ms, default 250 ms) — how long before the echo repeats
- `feedback` (0–0.99, default 0.3) — how much of the delay feeds back (higher = more repeats)
- `mix` (0–1, default 0.5) — wet/dry balance

Set `time` to **500 ms** and `feedback` to **0.5** for clearly audible eighth-note echoes at 120 BPM.

## Step 3: Try swapping the order

Effects order changes the sound significantly:

**Delay → Reverb** (current): Each delay echo reverberates individually. Clean separation.

**Reverb → Delay**: The reverb tail itself echoes. More diffuse, washy sound.

Disconnect and reconnect to hear the difference.

## What's happening

```
Oscillator → Gain → Delay → Reverb → audio_out
```

Each audio effect takes a buffer of samples as input and outputs a transformed buffer. The chain runs on the audio thread at sample rate — effects have no awareness of frame timing.

`mix` on each effect controls wet/dry blend. At `mix = 0` you hear only dry signal; at `mix = 1` you hear only the effect output. Most practical settings are 0.2–0.5 so the dry signal stays audible.

## Tip: Gain staging

Effects can boost signal level unexpectedly. If you hear distortion or clipping, lower the Gain `gain` parameter or add a second Gain after the effects to trim the output level. Keep the signal below 1.0 at each stage.

## Next steps

- [Control and Modulation](../control-and-modulation/) — automate Reverb mix or Delay time with an LFO
- [Audio-Reactive Visuals](../audio-reactive-visuals/) — use this audio chain to drive visuals
