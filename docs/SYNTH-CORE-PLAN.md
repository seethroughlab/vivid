# Core Synthesis Operators — Plan

Vivid ships with an Oscillator, Gain, Delay, Distortion, Bitcrush, and Reverb — and a
control-rate Envelope, LFO, and Clock. But none of the audio operators accept CV modulation
inputs, and there is no Filter or Noise source. The result: you cannot build even a basic
subtractive patch.

This document defines five targeted additions. Implement them in order — each one
independently unlocks more musical territory.

---

## What Already Exists (don't rebuild)

| Operator | File | Role |
|----------|------|------|
| Oscillator | `operators/audio/oscillator/oscillator.cpp` | VCO — sine/saw/square/triangle |
| Gain | `operators/audio/gain/gain.cpp` | Static amplitude scaling |
| Envelope | `operators/control/envelope/envelope.h` | ADSR — gate+phase → float CV |
| LFO | `operators/control/lfo/lfo.h` | Control-rate modulation source |
| Clock | `operators/control/clock/clock.h` | BPM/tempo, gate outputs |
| Delay | `operators/audio/delay/delay.cpp` | Feedback delay |
| Distortion | `operators/audio/distortion/distortion.cpp` | Soft-clip / tanh |
| Reverb | `operators/audio/reverb/reverb.cpp` | Freeverb algorithmic reverb |

DSP utilities already in `src/operator_api/audio_dsp.h`: `WhiteNoise`, `PinkNoise`,
`waveform()`. ADSR state machine in `src/operator_api/adsr.h`.

---

## Item 1 — CV Inputs on Oscillator and Gain

**Why first:** Enables the LFO → pitch and Envelope → amplitude connections that are the
foundation of every hardware patch. Without this, all other operators are islands.

**Problem:** `VIVID_PORT_FLOAT` ports are only used in control-domain operators today.
Audio operators (Oscillator, Gain) only expose audio ports or static parameters. The
runtime must support delivering a float value to an audio operator's float input port via
`VividAudioContext` — or this routing needs to be added.

**Prerequisite (runtime):** Verify or add support for `VIVID_PORT_FLOAT` input ports on
audio operators. The runtime needs to deliver the connected float value to the audio
operator somehow — likely a new field on `VividAudioContext` (e.g.,
`input_float_values[port_idx]`), or by treating the value as an overriding param.
This is the hardest part of Item 1 and must be confirmed before writing operator code.

**Oscillator changes** (`operators/audio/oscillator/oscillator.cpp`):
- Add `VIVID_PORT_FLOAT` input port `freq_cv` — semitone offset (±48 semitones), 0 = no
  modulation. Convert with `freq_hz * pow(2.0f, semitones / 12.0f)`.
- Add `VIVID_PORT_FLOAT` input port `amp_cv` — 0–1 linear scale factor, multiplied on
  top of the `amplitude` parameter. Default 1.0 when unconnected.
- Keep all existing params unchanged.

**Gain changes** (`operators/audio/gain/gain.cpp`):
- Add `VIVID_PORT_FLOAT` input port `amplitude_cv` — 0–1, multiplied by `gain` param.
  This makes Gain act as a VCA when driven by an Envelope or LFO.
- When unconnected, behaves exactly as before.

**Verification:** Connect LFO → Oscillator `freq_cv`; hear vibrato. Connect Envelope →
Gain `amplitude_cv`; hear amplitude shaping. Run `ctest` audio tests.

---

## Item 2 — Filter (VCF)

**Why second:** The defining element of subtractive synthesis. Without a filter you cannot
make classic Moog/Roland sounds. This is the single highest-impact addition.

**New file:** `operators/audio/filter/filter.cpp`

**Algorithm:** State Variable Filter (SVF) — numerically stable, one implementation
covers LP/HP/BP/notch by summing different state variables. Preferred over ladder filter
for simplicity and stability.

SVF per-sample update (Hal Chamberlin / Andy Simper formulation):
```
lowpass  += f * bandpass
highpass  = input - lowpass - q * bandpass
bandpass += f * highpass
notch     = highpass + lowpass
```
Where `f = 2 * sin(pi * cutoff / sample_rate)` and `q = 1 / resonance`.

**Parameters:**
- `cutoff` — float, 20–20000 Hz, default 2000 Hz
- `resonance` — float, 0.1–4.0, default 0.7 (self-oscillates above ~1.0 with SVF)
- `mode` — int enum: Low-pass / High-pass / Band-pass / Notch, default Low-pass

**Ports:**
- `input` — VIVID_PORT_AUDIO input (mono)
- `output` — VIVID_PORT_AUDIO output (mono)
- `cutoff_cv` — VIVID_PORT_FLOAT input — semitone offset from cutoff param (±72 st),
  0 = no modulation. Depends on Item 1 runtime work.
- `resonance_cv` — VIVID_PORT_FLOAT input — additive offset to resonance param (±2.0)

**State:** Two floats per channel: `low_` and `band_`. Lazy-initialize on sample-rate change.

**Semantic metadata:**
- `cutoff`: semantic_tag "frequency_hz", semantic_unit "Hz"
- `resonance`: semantic_tag "probability_01" (or add "resonance" tag)
- `cutoff_cv`: semantic_intent "filter_cutoff_cv"

**Thumbnail:** Draw a frequency response curve showing the LP slope and resonance peak,
similar to the Envelope thumbnail style (pixel-by-pixel rendering).

**Verification:**
- Connect Oscillator → Filter → Gain → output. Sweep cutoff by hand.
- Connect LFO → Filter `cutoff_cv`; hear filter sweep.
- Push resonance above 1.0; hear self-oscillation.

---

## Item 3 — Noise Source

**Why third:** Short to implement (DSP utilities already exist), high payoff — enables
percussive textures, wind sounds, and Sample & Hold workflows. Also useful blended into
the oscillator for a thicker sound.

**New file:** `operators/audio/noise/noise.cpp`

**Parameters:**
- `color` — int enum: White / Pink, default White
- `amplitude` — float, 0–1, default 0.5

**Ports:**
- `output` — VIVID_PORT_AUDIO output (mono)

**Implementation:** Instantiate `audio_dsp::WhiteNoise` and `audio_dsp::PinkNoise` as
member variables. Each call to `process_audio`, fill the output buffer from whichever
is selected. Pink noise output should be scaled — the Voss-McCartney sum of 8 octaves
needs a normalization factor (~0.25) to reach comparable levels to white noise.

**No input ports needed.** `kTimeDependent = false`.

**Thumbnail:** Draw a static noise waveform (a few cycles of random bars).

**Verification:** Connect Noise → Filter → Gain. Hear filtered noise.

---

## Item 4 — Mixer

**Why fourth:** Required to combine oscillator + noise, or layer multiple oscillators.
Without a mixer, you can only have one audio source in a chain.

**New file:** `operators/audio/mixer/mixer.cpp`

**Parameters:**
- `gain_1` through `gain_4` — float, 0–2, default 1.0 (shown as knobs in 2×2 layout)

**Ports:**
- `input_1` through `input_4` — VIVID_PORT_AUDIO inputs (mono), all optional
- `output` — VIVID_PORT_AUDIO output (mono)

**Implementation:** Sum `input_N[i] * gain_N` for all connected inputs. No clipping —
let the user manage levels. Unconnected inputs contribute 0. Check
`input_channel_counts[n]` (or a null-buffer convention) to detect unconnected ports.

**Layout:** Use `layout_row` and `VIVID_DISPLAY_KNOB` for the four gain knobs in a 2×2 grid.

**Verification:** Connect two Oscillators at different pitches → Mixer → Filter → Gain.
Hear both simultaneously.

---

## Item 5 — Slew Limiter (Lag / Portamento)

**Why fifth:** Smooths stepped control signals into glides — essential for:
- Portamento between notes (smooth pitch CV from stepped sequencer)
- Soft-knee on filter envelope modulation
- Turning a square LFO into a slow ramp

A hardware musician reaching for a Moog or Buchla patch will immediately want portamento.

**Note:** A control-rate `Smooth` operator already exists at
`operators/control/smooth/smooth.h`. Check if it's exposed and visible to users. If it
is, Item 5 may already be done — or a more flexible version with independent rise/fall
times may be worth adding as an audio-domain slew limiter for audio-rate signals.

**New file (if needed):** `operators/control/slew/slew.cpp` (control-rate) or an audio-
rate version at `operators/audio/slew/slew.cpp`.

**Parameters (control-rate version):**
- `rise_time` — float, 0–5 s, time to reach target from below
- `fall_time` — float, 0–5 s, time to reach target from above

**Ports:**
- `input` — VIVID_PORT_FLOAT input
- `output` — VIVID_PORT_FLOAT output

**Implementation (control-rate):** Per-frame: if `target > current`, advance at rate
`(target - current) * (1 / rise_time) * dt`; similarly for fall. Clamp.

**Verification:** Connect Clock gate → Slew → Oscillator `freq_cv`. Hear pitch glide
instead of step. Connect stepped random → Slew; hear smooth random walk.

---

## Minimal Patch — "Classic Subtractive Drone"

Once all five items are implemented, this patch should work out of the box:

```
Clock (120 BPM)
  ├─ gate ──────────────────────────────→ Envelope (gate)
  └─ (phase optional)

Noise ──────────────────────────────────┐
                                        ├─→ Mixer ──→ Filter ──→ Gain ──→ Output
Oscillator (110 Hz, saw) ───────────────┘              ↑            ↑
                                               Envelope*400Hz  Envelope*0.8
LFO (0.3 Hz, sine) ──→ Oscillator freq_cv (±3 semitones vibrato)

Envelope (A:5ms D:200ms S:0.6 R:800ms):
  ├─ value ──→ Filter cutoff_cv (scale: 0–400 Hz above base 400 Hz)
  └─ value ──→ Gain amplitude_cv (full range 0–1)
```

This patch produces: a low saw-wave drone with a bit of noise texture, filter opens on
each beat driven by envelope, vibrato from LFO, amplitude shaped by same envelope.
Classic hardware techno/ambient sound.

**Demo graph file:** Create a demo graph at `demo_graphs/subtractive_drone.vivid` (or
equivalent path) after all operators are implemented.

---

## Implementation Order

1. **Runtime: float ports on audio operators** — prerequisite for CV routing
2. **Oscillator + Gain CV inputs** (Item 1)
3. **Filter** (Item 2) — highest musical impact
4. **Noise** (Item 3) — quick win
5. **Mixer** (Item 4)
6. **Slew / verify Smooth exists** (Item 5)
7. **Demo graph** — minimal patch

---

## Key Files

| File | Purpose |
|------|---------|
| `src/operator_api/types.h` | VividAudioContext, VividPortType — may need new field for float inputs |
| `src/operator_api/audio_dsp.h` | WhiteNoise, PinkNoise — use directly in Noise operator |
| `src/operator_api/adsr.h` | ADSR state machine — Envelope already uses this |
| `operators/audio/oscillator/oscillator.cpp` | Add freq_cv, amp_cv ports |
| `operators/audio/gain/gain.cpp` | Add amplitude_cv port |
| `operators/audio/filter/filter.cpp` | New — SVF filter |
| `operators/audio/noise/noise.cpp` | New — white/pink noise |
| `operators/audio/mixer/mixer.cpp` | New — 4-input mixer |
| `operators/control/smooth/smooth.h` | Check if Slew already exists here |
| `CMakeLists.txt` (operators/audio/) | Register new operators |
