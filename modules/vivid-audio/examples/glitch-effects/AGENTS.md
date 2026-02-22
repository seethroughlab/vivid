# Glitch Effects Example

Demonstrates time-domain audio glitch effects applied to a drum pattern.

## Operators Demonstrated

- **BeatRepeat** - Capture and loop audio slices at musical intervals
- **Reverse** - Play audio slices backwards
- **Stutter** - Rhythmic micro-repeats with volume envelopes
- **TapeStop** - Tape deck stop/start pitch effects
- **Scratch** - DJ-style variable-speed playback

## Key Concepts

### Probability-Based Triggering
All glitch effects use a `chance` parameter (0-1) to randomly decide whether to trigger on each clock division:

```cpp
auto& repeat = chain.add<BeatRepeat>("repeat");
repeat.input("drums");
repeat.bpm = 128.0f;
repeat.triggerDiv(ClockDiv::Eighth);   // Check every eighth note
repeat.sliceDiv(ClockDiv::Sixteenth);  // Slice length
repeat.chance = 0.15f;                 // 15% trigger probability
repeat.repeatCount = 4;                // Repeat 4 times
```

### Stutter Envelopes
Stutter supports volume shaping over the stutter duration:

```cpp
stutter.envelope(StutterEnvelope::Decay);     // Gets quieter (classic)
stutter.envelope(StutterEnvelope::Build);     // Gets louder (buildup)
stutter.envelope(StutterEnvelope::Triangle);  // Quiet-loud-quiet
stutter.envelope(StutterEnvelope::Flat);      // No volume change
```

### TapeStop Modes
```cpp
tape.mode(TapeMode::Stop);       // Slow down to stop only
tape.mode(TapeMode::Start);      // Speed up from stop only
tape.mode(TapeMode::StopStart);  // Slow down, then speed back up
```

### Scratch Motions
```cpp
scratch.motion(ScratchMotion::Forward);    // Variable speed forward
scratch.motion(ScratchMotion::Backward);   // Variable speed backward
scratch.motion(ScratchMotion::BackForth);  // Classic DJ scratch
scratch.motion(ScratchMotion::Random);     // Random direction changes
```

### Clock Divisions
All glitch effects use tempo-synced divisions:
- `ClockDiv::Whole` through `ClockDiv::ThirtySecond`
- `ClockDiv::DottedQuarter`, `ClockDiv::DottedEighth`
- `ClockDiv::TripletQuarter`, `ClockDiv::TripletEighth`

## Related Operators

- **Glitch** - Master processor combining all effects (see glitch-master example)
- **FrequencyShift** - Inharmonic frequency shifting
- **Stretch** - Granular time-stretching
