# DSP Utilities

Header-only DSP building blocks for audio and control operators.

## audio_dsp.h — Core Oscillators and Noise

```cpp
#include "operator_api/audio_dsp.h"
```

### WhiteNoise
LCG-based PRNG producing float samples.
```cpp
audio_dsp::WhiteNoise noise;
float sample = noise.next();          // [-1, 1]
float uni    = noise.next_unipolar(); // [0, 1]
```

### PinkNoise
Voss-McCartney algorithm (8 octave bands), -3 dB/octave rolloff.
```cpp
audio_dsp::PinkNoise pink;
float sample = pink.next();  // [-1, 1]
```

### waveform(phase, type)
Classic waveforms from a [0,1) phase input.
```cpp
// type: 0=sine, 1=saw, 2=square, 3=triangle
double out = audio_dsp::waveform(phase, type);  // returns [-1, 1]
```

### harmonics_3(phase, amount)
3-harmonic sine mixing: fundamental + 2nd harmonic (0.5×) + 3rd harmonic (0.25×). `amount` blends from pure sine (0) to full mix (1).

### ring_osc_bank(phases, freqs, count, pitch_mult, inv_sr)
Square-wave ring oscillator bank — advances N phases in-place, returns normalized sum.

### detect_trigger(phase, prev_phase)
Returns `true` when phase wraps (delta < -0.5).

## drum_dsp.h — Percussion Building Blocks

```cpp
#include "operator_api/drum_dsp.h"
```

### DecayEnvelope
Exponential decay: `exp(-t × 5.0 / decay_seconds)`.
```cpp
drum::DecayEnvelope env;
env.trigger();                      // reset to t=0
env.advance(1.0 / sample_rate);     // tick
float level = env.value(0.3f);      // 0.3s decay time
```

### SVF (State Variable Filter)
Chamberlin 2-pole filter with LP/HP/BP modes.
```cpp
drum::SVF filter;
float out = filter.process(input, cutoff_hz, resonance, sample_rate, drum::SVF::LP);
// resonance: 0.0 (max resonance) to 1.0 (no resonance)
// Stability: cutoff clamped internally via 2*sin(π*f/sr) ≤ 0.95
```

### soft_clip(x, drive)
Soft saturation: `tanh(x × (1 + drive × 3))`.

### PinkNoise (drum namespace)
Same Voss-McCartney algorithm as `audio_dsp::PinkNoise`.

## adsr.h — ADSR Envelope

```cpp
#include "operator_api/adsr.h"
```

Linear ADSR envelope with 5 stages: IDLE, ATTACK, DECAY, SUSTAIN, RELEASE.

```cpp
vivid::adsr::State env;

// Gate on → starts attack
vivid::adsr::gate_on(env);

// Per-sample advance
vivid::adsr::advance(env, dt, attack_s, decay_s, sustain_level, release_s);
float level = env.env_value;  // current envelope value [0, 1]

// Gate off → starts release from current level
vivid::adsr::gate_off(env);

// Check if still active
if (env.is_active()) { /* still producing output */ }
```

All time parameters are in seconds. Minimum time is clamped to 1ms internally.
