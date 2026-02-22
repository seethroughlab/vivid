# Envelopes Example

Demonstrates all four envelope types applied to different musical contexts.

## Operators Demonstrated

- **Envelope** - Full ADSR envelope for sustained sounds
- **AR** - Two-stage attack-release for one-shot percussive sounds
- **PitchEnv** - Frequency sweep envelope for kick drums and effects
- **ADSRMod** - Per-voice ADSR modulator for synth parameter control

## Key Concepts

### Envelope (ADSR)
Full four-stage envelope with sustained hold:

```cpp
auto& env = chain.add<Envelope>("env");
env.input("oscillator");      // Multiplies audio by envelope
env.attack = 0.3f;            // Ramp up time
env.decay = 0.2f;             // Fall to sustain level
env.sustain = 0.6f;           // Hold level (0-1)
env.release = 0.8f;           // Fade out time

env.trigger();                // Start attack phase
env.releaseNote();            // Start release phase
```

### AR (Attack-Release)
Simplified two-stage envelope — no sustain phase. One-shot: fires and forgets.

```cpp
auto& ar = chain.add<AR>("pluck");
ar.input("oscillator");
ar.attack = 0.005f;           // Very fast attack
ar.release = 0.4f;            // Medium release

ar.setTriggerSource("seq");   // Audio-thread triggering
```

### PitchEnv (Frequency Sweep)
Generates frequency values, not audio. Read `currentFreq()` and apply manually:

```cpp
auto& pitchEnv = chain.add<PitchEnv>("pitch");
pitchEnv.startFreq = 150.0f;  // Start frequency (Hz)
pitchEnv.endFreq = 50.0f;     // End frequency (Hz)
pitchEnv.time = 0.1f;         // Sweep duration (seconds)

pitchEnv.setTriggerSource("seq");

// In update():
osc.frequency = pitchEnv.currentFreq();
```

### ADSRMod (Per-Voice Modulator)
Attach to a synth for per-voice parameter modulation:

```cpp
auto& synth = chain.add<WavetableSynth>("synth");
synth.filterCutoff = 500.0f;

// Attach ADSR modulator
auto& filterEnv = synth.addModulator<ADSRMod>("filterEnv");
filterEnv.attack = 0.01f;
filterEnv.decay = 0.35f;
filterEnv.sustain = 0.15f;
filterEnv.release = 0.4f;
filterEnv.perVoice = true;       // Each voice gets own envelope

// Route to filter cutoff (unipolar: envelope range 0→1)
synth.modulate(filterEnv, "filterCutoff", 0.8f, false);
```

### Envelope vs AR vs ADSRMod

| Feature | Envelope | AR | ADSRMod |
|---------|----------|----|---------|
| Stages | 4 (ADSR) | 2 (AR) | 4 (ADSR) |
| Sustain | Yes | No | Yes |
| Audio input | Yes | Yes | No (modulator) |
| Per-voice | No | No | Yes (when attached) |
| MIDI | Yes | Yes | Via synth |
| Use case | Pads, held notes | Plucks, percussion | Filter sweeps |

### Standalone vs Attached ADSRMod
ADSRMod can also be used standalone (not attached to a synth):

```cpp
auto& env = chain.add<ADSRMod>("env");
env.trigger();
env.releaseNote();
float val = env.value();     // Current value [0-1]
```

## Related Operators

- **Oscillator** - Audio source for envelopes to shape
- **WavetableSynth** - Host for ADSRMod per-voice modulation
- **LFO** - Periodic modulator (vs. one-shot envelopes)
- **Decay** - Single-stage exponential decay
