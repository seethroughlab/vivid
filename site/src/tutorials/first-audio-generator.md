## What you'll build

A simple synthesizer: one oscillator at 440 Hz running through a gain stage into the audio output. You'll hear your first tone and learn how audio operators chain together.

## Operators used

| Operator | Domain | Role |
|----------|--------|------|
| Oscillator | Audio | Generates a periodic waveform |
| Gain | Audio | Scales signal amplitude |
| audio_out | — | Sends audio to the system output |

## Step 1: Create a new graph

Choose **File → New Graph**. The canvas opens empty.

## Step 2: Add an Oscillator

Add **Oscillator**. Set its parameters:
- `frequency` → **440** (A4, the standard tuning reference)
- `amplitude` → **0.5** (half volume — good starting point)
- `waveform` → **sine** (purest tone, no harmonics)

## Step 3: Add Gain

Add **Gain**. Connect `osc1/output` → `gain1/input`. Set `gain` to **0.8**.

The Gain node gives you a master volume control independent of the oscillator's own amplitude.

## Step 4: Connect to audio output

Add **audio_out** (or use the one already on the canvas). Connect `gain1/output` → `audio_out/input`.

Press play. You should hear a 440 Hz sine tone.

## Step 5: Explore waveforms

Change Oscillator `waveform` and listen to the difference:

| Waveform | Character |
|----------|-----------|
| sine | Pure tone, no harmonics |
| saw | Bright, buzzy — all harmonics present |
| square | Hollow, reed-like — odd harmonics only |
| triangle | Softer than square, fewer high harmonics |

## Step 6: Map frequencies to musical notes

The Oscillator uses Hz. Common reference points:

| Note | Frequency |
|------|-----------|
| A4 | 440 Hz |
| A3 | 220 Hz |
| A5 | 880 Hz |
| C4 (middle C) | 261.6 Hz |
| E4 | 329.6 Hz |

Every octave doubles the frequency.

## What's happening

```
Oscillator (Audio) → Gain (Audio) → audio_out
```

Audio operators process blocks of samples at ~48,000 samples per second on a dedicated real-time thread. Each operator in the chain receives a buffer of samples, processes it, and passes the result downstream. The `audio_out` node submits the final buffer to the system audio device.

Volume is controlled at two places: `amplitude` on the Oscillator sets the raw output level, and `gain` on the Gain node scales the whole chain. Keeping Oscillator amplitude below 1.0 leaves headroom for effects and mixing.

## Next steps

- [Your First Audio Effect](../first-audio-effect/) — add reverb and delay to this signal
- [Control and Modulation](../control-and-modulation/) — modulate oscillator frequency with an LFO
- [Using MIDI Input](../midi-input/) — play notes from a keyboard instead of a fixed frequency
