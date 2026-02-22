# Glitch Master Example

Demonstrates the master Glitch processor and standalone frequency/time effects.

## Operators Demonstrated

- **Glitch** - Master multi-effect processor with probabilistic triggering
- **FrequencyShift** - Bode frequency shifter for metallic, inharmonic textures
- **Stretch** - Granular time-stretching without pitch change

## Key Concepts

### Glitch Master Processor
The Glitch operator combines all glitch effects (BeatRepeat, Reverse, Stutter, TapeStop, Scratch, FrequencyShift) into a single processor. Only one effect plays at a time, selected by probability:

```cpp
auto& glitch = chain.add<Glitch>("glitch");
glitch.input("source");
glitch.bpm = 120.0f;
glitch.triggerDiv(ClockDiv::Quarter);
glitch.repeatChance = 0.2f;     // BeatRepeat probability
glitch.reverseChance = 0.15f;   // Reverse probability
glitch.stutterChance = 0.15f;   // Stutter probability
glitch.scratchChance = 0.1f;    // Scratch probability
glitch.tapeChance = 0.08f;      // TapeStop probability
glitch.shiftChance = 0.1f;      // FrequencyShift probability
```

### FrequencyShift
Unlike pitch shifting, frequency shifting adds a fixed Hz offset to all frequencies. This creates inharmonic, metallic textures:

```cpp
auto& freqShift = chain.add<FrequencyShift>("shift");
freqShift.input("source");
freqShift.shift = 50.0f;          // Shift up 50 Hz
freqShift.modDepth = 30.0f;       // LFO modulation depth in Hz
freqShift.modDiv(ClockDiv::Quarter);  // LFO rate
freqShift.mix = 0.5f;
```

### Granular Time-Stretch
Stretch uses overlapping grains to change playback speed without altering pitch:

```cpp
auto& stretch = chain.add<Stretch>("stretch");
stretch.input("source");
stretch.bpm = 120.0f;
stretch.stretchFactor = 2.0f;     // Half-speed (longer duration)
stretch.grainSize = 60.0f;        // 60ms grains
stretch.grainRandom = 0.1f;       // Position randomization
stretch.overlap = 0.5f;           // 50% grain overlap
stretch.chance = 0.25f;           // 25% trigger probability
```

## Related Operators

- **BeatRepeat**, **Reverse**, **Stutter**, **TapeStop**, **Scratch** - Individual glitch effects (see glitch-effects example)
- **Granular** - Full granular synthesis engine
