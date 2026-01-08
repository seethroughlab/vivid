# Envelope Modulation

Demonstrates envelope generators for shaping sound over time.

## Operators Used

- **Envelope** - Full ADSR envelope generator
- **AR** - Simplified Attack-Release envelope
- **PitchEnv** - Pitch sweep envelope (for kicks/FX)
- **Oscillator** - Sound source
- **Filter** - Envelope-modulated filter

## Key Concepts

### ADSR Envelope (Envelope)

Four-stage envelope for sustained sounds:
```cpp
auto& env = chain.add<Envelope>("env");
env.input("osc");
env.attack = 0.1f;    // Rise time (0 to max)
env.decay = 0.2f;     // Fall to sustain level
env.sustain = 0.6f;   // Hold level (0-1)
env.release = 0.5f;   // Fall to zero after release

// Control
env.trigger();       // Start envelope (note on)
env.releaseNote();   // Begin release phase (note off)

// Query state
float val = env.currentValue();     // 0-1
EnvelopeStage stage = env.stage();  // Idle/Attack/Decay/Sustain/Release
bool active = env.isActive();
```

### AR Envelope

Two-stage envelope for plucks and percussion:
```cpp
auto& ar = chain.add<AR>("ar");
ar.input("osc");
ar.attack = 0.005f;   // Very fast attack
ar.release = 0.3f;    // Decay time

ar.trigger();         // One-shot trigger
float val = ar.currentValue();
bool active = ar.isActive();
```

### Pitch Envelope (PitchEnv)

Frequency sweep for kicks, toms, and special FX:
```cpp
auto& pitch = chain.add<PitchEnv>("pitch");
pitch.startFreq = 200.0f;   // Starting frequency (Hz)
pitch.endFreq = 50.0f;      // Ending frequency (Hz)
pitch.time = 0.15f;         // Sweep duration (seconds)

pitch.trigger();
float freq = pitch.currentFreq();  // Use to modulate oscillator
osc.frequency(freq);
```

## Envelope Stages

### ADSR Flow
```
         Attack    Decay
           /\
          /  \      Sustain
         /    \____________________
        /                          \
       /                            \  Release
______/                              \______
      ^                              ^
   trigger()                   releaseNote()
```

### AR Flow
```
         Attack
           /\
          /  \
         /    \  Release
        /      \
       /        \
______/          \______
      ^
   trigger()
```

## Common Patterns

### Plucky Synth
```cpp
auto& ar = chain.add<AR>("ar");
ar.attack = 0.001f;   // Instant attack
ar.release = 0.15f;   // Quick decay
```

### Pad Sound
```cpp
auto& env = chain.add<Envelope>("env");
env.attack = 0.5f;    // Slow fade in
env.decay = 0.3f;
env.sustain = 0.8f;   // High sustain
env.release = 1.0f;   // Long fade out
```

### Kick Drum
```cpp
auto& pitch = chain.add<PitchEnv>("pitch");
pitch.startFreq = 150.0f;
pitch.endFreq = 50.0f;
pitch.time = 0.1f;

auto& amp = chain.add<AR>("amp");
amp.attack = 0.001f;
amp.release = 0.4f;
```

### Filter Envelope
```cpp
// Modulate filter cutoff with envelope value
float envVal = env.currentValue();
filter.cutoff = 200.0f + envVal * 4000.0f;
```

### Velocity Sensitivity
```cpp
// Scale envelope output by velocity
float velocity = 0.8f;  // From MIDI
float envVal = env.currentValue() * velocity;
```

## Controls

- **Keys 1/2/3** - Select demo (ADSR, AR, PitchEnv)
- **Space / Mouse Click** - Trigger envelope
- **Release Space / Mouse** - Release envelope (ADSR only)
- **Mouse Y** - Adjust parameter (sustain, release time, or start freq)

## Tips

1. **Fast attacks** (< 10ms) sound percussive
2. **Slow attacks** (> 100ms) sound pad-like
3. **High sustain** for organ-like sounds
4. **Zero sustain** for plucks (similar to AR)
5. **Pitch envelopes** add punch to kicks (sweep down) or risers (sweep up)
