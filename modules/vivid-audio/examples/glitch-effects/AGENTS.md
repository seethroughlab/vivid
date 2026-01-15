# Glitch Effects

Demonstrates tempo-synced audio manipulation effects for creative production.

## What This Example Shows

- **Effect selection**: Move mouse horizontally to switch between 8 glitch effects
- **Parameter control**: Move mouse vertically to adjust the active effect's main parameter
- **Visual feedback**: Audio level meters and effect indicators
- **Drum pattern**: Basic four-on-the-floor beat as audio source

## Operators Used

### Audio Sources
- **Clock** - Tempo sync at 120 BPM
- **Kick, Snare, HiHat** - Drum synthesis
- **AudioMixer** - Mix drums together

### Glitch Effects
- **BeatRepeat** - Loop audio slices with decay
- **Reverse** - Play audio backwards
- **Stutter** - Rapid repeats with volume envelope
- **Scratch** - DJ-style varispeed playback
- **TapeStop** - Turntable slowdown/speedup
- **FrequencyShift** - Bode frequency shifting
- **Stretch** - Granular time-stretch
- **Glitch** - Meta-effect combining all above

### Analysis
- **Levels** - RMS and peak level metering

### Visual
- **Canvas** - 2D drawing for UI

## Key Concepts

### BeatRepeat
Captures and loops audio slices rhythmically:
```cpp
auto& repeat = chain.add<BeatRepeat>("repeat");
repeat.input("audio");
repeat.bpm = 120.0f;
repeat.triggerDiv(ClockDiv::Quarter);   // Check every beat
repeat.sliceDiv(ClockDiv::Sixteenth);   // 16th note slices
repeat.repeatCount = 4;                  // Loop 4 times
repeat.decay = 0.15f;                    // Volume decay per repeat
repeat.chance = 0.3f;                    // 30% probability
```

### Reverse
Plays captured audio backwards:
```cpp
auto& reverse = chain.add<Reverse>("reverse");
reverse.input("audio");
reverse.bpm = 120.0f;
reverse.triggerDiv(ClockDiv::Half);     // Check every half bar
reverse.reverseDiv(ClockDiv::Quarter);  // Reverse a quarter note
reverse.chance = 0.2f;
reverse.mix = 1.0f;                     // Fully wet
```

### Stutter
Rapid repeats with volume envelope for build-ups:
```cpp
auto& stutter = chain.add<Stutter>("stutter");
stutter.input("audio");
stutter.bpm = 120.0f;
stutter.triggerDiv(ClockDiv::Half);
stutter.stutterDiv(ClockDiv::ThirtySecond);  // Fast 32nd notes
stutter.stutterCount = 8;
stutter.envelope(StutterEnvelope::Build);     // Gets louder
stutter.envAmount = 0.7f;
stutter.chance = 0.15f;
```

Envelope types:
- `StutterEnvelope::Flat` - No volume change
- `StutterEnvelope::Decay` - Gets quieter (classic stutter)
- `StutterEnvelope::Build` - Gets louder (rise effect)
- `StutterEnvelope::Triangle` - Quiet → loud → quiet

### Scratch
DJ-style varispeed with direction changes:
```cpp
auto& scratch = chain.add<Scratch>("scratch");
scratch.input("audio");
scratch.bpm = 120.0f;
scratch.triggerDiv(ClockDiv::Half);
scratch.motion(ScratchMotion::BackForth);  // Back and forth
scratch.speed = 1.2f;                       // 1.2x playback speed
scratch.speedRandom = 0.3f;                 // ±30% randomization
scratch.scratchBeats = 0.5f;               // Half beat duration
scratch.chance = 0.1f;
```

Motion types:
- `ScratchMotion::Forward` - Normal speed playback
- `ScratchMotion::Backward` - Reverse playback
- `ScratchMotion::BackForth` - Oscillating direction
- `ScratchMotion::Random` - Random direction changes

### TapeStop
Simulates tape deck slowing down or speeding up:
```cpp
auto& tape = chain.add<TapeStop>("tape");
tape.input("audio");
tape.bpm = 120.0f;
tape.triggerDiv(ClockDiv::Whole);
tape.mode(TapeMode::StopStart);  // Slow down then speed back up
tape.stopTime = 400.0f;          // 400ms to stop
tape.startTime = 200.0f;         // 200ms to restart
tape.chance = 0.08f;
```

Modes:
- `TapeMode::Stop` - Slow to silence
- `TapeMode::Start` - Speed up from silence
- `TapeMode::StopStart` - Stop then restart

### FrequencyShift
Bode-style frequency shifting (different from pitch shift):
```cpp
auto& freq = chain.add<FrequencyShift>("freq");
freq.input("audio");
freq.shift = 30.0f;              // Shift all frequencies by 30 Hz
freq.bpm = 120.0f;
freq.modDiv(ClockDiv::Quarter);  // Modulation sync
freq.modDepth = 20.0f;           // ±20 Hz modulation
freq.mix = 0.7f;                 // 70% wet
```

Frequency shifting moves all frequencies by a fixed Hz amount, creating inharmonic, metallic textures. Unlike pitch shifting, it doesn't preserve harmonic relationships.

### Stretch
Granular time-stretch without pitch change:
```cpp
auto& stretch = chain.add<Stretch>("stretch");
stretch.input("audio");
stretch.bpm = 120.0f;
stretch.triggerDiv(ClockDiv::Whole);
stretch.stretchDiv(ClockDiv::Quarter);  // Source length
stretch.stretchFactor = 2.0f;            // Half speed (2x duration)
stretch.grainSize = 60.0f;               // 60ms grains
stretch.grainRandom = 0.1f;              // 10% position randomization
stretch.overlap = 0.5f;                  // 50% grain overlap
stretch.chance = 0.15f;
```

Uses overlapping Hann-windowed grains to change playback duration while maintaining pitch.

### Glitch (Meta-Effect)
Combines all effects with per-effect probability:
```cpp
auto& glitch = chain.add<Glitch>("glitch");
glitch.input("audio");
glitch.bpm = 120.0f;
glitch.triggerDiv(ClockDiv::Quarter);

// Set probability for each effect type
glitch.repeatChance = 0.2f;    // 20% BeatRepeat
glitch.reverseChance = 0.15f;  // 15% Reverse
glitch.stutterChance = 0.15f;  // 15% Stutter
glitch.scratchChance = 0.1f;   // 10% Scratch
glitch.tapeChance = 0.08f;     // 8% TapeStop
glitch.shiftChance = 0.1f;     // 10% FrequencyShift
glitch.mix = 1.0f;
```

Only one effect plays at a time. When the trigger clock fires, the Glitch operator randomly selects which effect to activate based on the probability weights.

## Controls

- **Mouse X** - Select effect (0-7)
- **Mouse Y** - Adjust effect intensity parameter

## Clock Divisions

All glitch effects use `ClockDiv` for tempo sync:

| Division | Musical Value |
|----------|---------------|
| `ClockDiv::Whole` | 1 bar |
| `ClockDiv::Half` | 1/2 bar |
| `ClockDiv::Quarter` | 1 beat |
| `ClockDiv::Eighth` | 1/2 beat |
| `ClockDiv::Sixteenth` | 1/4 beat |
| `ClockDiv::ThirtySecond` | 1/8 beat |
| `ClockDiv::DottedQuarter` | 1.5 beats |
| `ClockDiv::DottedEighth` | 3/4 beat |
| `ClockDiv::TripletQuarter` | 2/3 beat |
| `ClockDiv::TripletEighth` | 1/3 beat |

## Common Patterns

### Classic Stutter Build
```cpp
stutter.stutterDiv(ClockDiv::ThirtySecond);
stutter.stutterCount = 16;
stutter.envelope(StutterEnvelope::Build);
stutter.envAmount = 1.0f;
```

### Subtle Glitch Layer
```cpp
glitch.repeatChance = 0.1f;
glitch.stutterChance = 0.05f;
glitch.tapeChance = 0.02f;
// Low probabilities for occasional surprises
```

### Aggressive Glitch
```cpp
glitch.triggerDiv(ClockDiv::Eighth);  // More frequent checks
glitch.repeatChance = 0.4f;
glitch.stutterChance = 0.3f;
glitch.scratchChance = 0.2f;
```

### Tape Stop Transition
```cpp
tape.mode(TapeMode::Stop);
tape.stopTime = 800.0f;  // Long dramatic slowdown
tape.chance = 1.0f;      // Trigger manually
```

### Granular Freeze
```cpp
stretch.stretchFactor = 4.0f;  // 4x slower
stretch.grainSize = 100.0f;    // Larger grains
stretch.grainRandom = 0.3f;    // More variation
```

## Effect Chaining

Effects can be chained for complex processing:
```cpp
repeat.input("drums");
reverse.input("repeat");    // Reverse the repeated audio
stretch.input("reverse");   // Then stretch it
```

Or use the Glitch meta-effect for automatic variety:
```cpp
glitch.input("drums");  // One operator, 6 effects
```
