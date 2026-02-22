# Multi-Sampler Example

Demonstrates the full-featured multi-zone sampler with velocity layers and key zones.

## Operators Demonstrated

- **MultiSampler** - Multi-zone sampler with key ranges, velocity layers, and per-region tuning

## Key Concepts

### Manual Region Setup
Define key zones by creating SampleRegion structs:

```cpp
auto& multi = chain.add<MultiSampler>("instrument");

SampleRegion region;
region.path = "samples/piano_c4.wav";
region.rootNote = 60;        // Original pitch of the sample
region.loNote = 48;          // Lowest MIDI note this region responds to
region.hiNote = 71;          // Highest MIDI note
region.loVel = 0;            // Lowest velocity (0-127)
region.hiVel = 127;          // Highest velocity
region.volumeDb = 0.0f;      // Volume adjustment in dB
region.pan = 0.0f;           // Pan (-1 left, 0 center, 1 right)
region.tuneCents = 0;        // Fine tuning in cents
multi.addRegion(region);
```

### Velocity Layers
Create multiple regions for the same key range with different velocity ranges:

```cpp
SampleRegion soft;
soft.path = "samples/piano_soft.wav";
soft.loNote = 60; soft.hiNote = 72;
soft.loVel = 0; soft.hiVel = 63;      // Soft hits
multi.addRegion(soft);

SampleRegion hard;
hard.path = "samples/piano_hard.wav";
hard.loNote = 60; hard.hiNote = 72;
hard.loVel = 64; hard.hiVel = 127;    // Hard hits
multi.addRegion(hard);
```

### Velocity Curve
Control how input velocity maps to output volume:

```cpp
multi.velCurve = -1.0f;  // Soft: gentle velocity response
multi.velCurve = 0.0f;   // Linear: direct mapping (default)
multi.velCurve = 1.0f;   // Hard: aggressive response
```

### Preset Loading
Load from JSON or Decent Sampler format:

```cpp
multi.loadPreset("assets/instrument/preset.json");
multi.loadDspreset("assets/instrument/preset.dspreset");
```

### Groups and Keyswitches
Organize regions into groups for articulation switching:

```cpp
SampleGroup sustain;
sustain.name = "Sustain";
sustain.keyswitch = 24;    // C1 activates this group
sustain.regions = { ... };
multi.addGroup(sustain);

SampleGroup staccato;
staccato.name = "Staccato";
staccato.keyswitch = 25;   // C#1 activates this group
staccato.regions = { ... };
multi.addGroup(staccato);

// Switch articulation
multi.setKeyswitch(24);    // Switch to sustain
multi.setActiveGroup(1);   // Or by index
```

### Loop Settings
Per-region loop control:

```cpp
SampleRegion region;
region.loopEnabled = true;
region.loopStart = 48000;      // In samples
region.loopEnd = 96000;        // In samples (0 = end of file)
region.loopCrossfade = 1024;   // Crossfade length in samples
```

## Related Operators

- **Sampler** - Simpler single-sample chromatic player (see sampler-basics)
- **SampleBank** + **SamplePlayer** - Index/name-based triggering (see sampler-basics)
