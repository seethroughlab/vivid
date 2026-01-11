# Drum Showcase

Demonstrates all 9 drum synthesis operators with sequenced patterns and manual triggering.

## Operators Used

### Original Drums (Enhanced)
- **Kick** - 808-style with pitch envelope, overtones, attack control
- **Snare** - Tone/noise mix with color shaping, selectable filter type
- **HiHat** - Metallic with noise type selection, filter slope, pitch multiplier
- **Clap** - Multi-burst with stereo width, filtered tail, tunable filter

### New Drums
- **Tom** - Resonant body with pitch bend and color
- **Cymbal** - Crash/ride with 12 ring oscillators and shimmer
- **FMDrum** - 2-operator FM for metallic/bell sounds
- **Clang** - Cowbell/clave with dual square oscillators
- **DrumStack** - Layer multiple drum voices together

## Controls (ImGui Panel)

### Transport
- **Play/Stop** button - Start/stop the sequencer
- **BPM** slider - Adjust tempo (60-200)
- **Swing** slider - Add shuffle feel (0-0.5)

### Pattern Selection
- Dropdown to select A/B/C/D patterns

### Manual Triggers
- 9 buttons in a 3x3 grid to trigger each drum voice manually

## Patterns

### Pattern A - Standard Kit
Classic four-on-the-floor with Kick, Snare, HiHat, Clap.
```
Kick:  X...X...X...X...
Snare: ....X.......X...
HiHat: XXXXXXXXXXXXXXXX
Clap:  ....X.......X...
```

### Pattern B - Extended Kit
Adds Tom fills, Cymbal crashes, and Clang accents.
```
Kick:  X...X...X...X...
Snare: ....X.......X...
HiHat: X.X.X.X.X.X.X.X.
Tom:   ..X.......X.....
Cymbal:...............X
Clang: .X.......X......
```

### Pattern C - FM/Experimental
Features FMDrum and DrumStack layering.
```
Kick:  X...X...X..XX...
HiHat: .X.X.X.X.X.X.X.X
FMDrum:..X..X....X..X..
Stack: ....X.......X...
```

### Pattern D - Full Kit
All 9 drums active together.

## Key Parameters

### Kick
```cpp
kick.pitch = 55.0f;       // Base frequency
kick.pitchEnv = 120.0f;   // Pitch sweep amount
kick.decay = 0.4f;        // Amplitude decay
kick.overtones = 0.2f;    // Harmonic content (NEW)
kick.attack = 0.0f;       // Attack softening (NEW)
```

### Snare
```cpp
snare.tone = 0.5f;        // Body mix
snare.noise = 0.7f;       // Noise mix
snare.color = 0.6f;       // Harmonic richness (NEW)
snare.filterType(SnareFilterType::Highpass);  // Filter mode (NEW)
```

### HiHat
```cpp
hihat.decay = 0.05f;      // Short = closed
hihat.noiseType(NoiseType::White);   // White/Pink (NEW)
hihat.filterSlope(FilterSlope::Slope12dB);  // 12dB/24dB (NEW)
hihat.pitch = 1.0f;       // Ring pitch multiplier (NEW)
```

### Clap
```cpp
clap.sloppy = 0.04f;      // Timing spread (renamed from spread)
clap.tail = 0.3f;         // Filtered tail (NEW)
clap.stereoWidth = 0.4f;  // Stereo spread (NEW)
clap.tune = 1800.0f;      // Filter frequency (NEW)
```

### Tom
```cpp
tom.pitch = 120.0f;       // Base frequency
tom.bend = 0.6f;          // Pitch envelope amount
tom.tone = 0.5f;          // Resonant filter
tom.color = 0.4f;         // Harmonic content
```

### Cymbal
```cpp
cymbal.pitch = 1.0f;      // Ring oscillator pitch
cymbal.decay = 2.5f;      // Long decay (0.5-10s)
cymbal.shimmer = 0.3f;    // LFO modulation
```

### FMDrum
```cpp
fmdrum.pitch = 180.0f;    // Carrier frequency
fmdrum.ratio = 2.5f;      // Modulator ratio
fmdrum.amount = 0.7f;     // FM depth
fmdrum.feedback = 0.2f;   // Self-modulation
```

### Clang
```cpp
clang.pitch = 800.0f;     // Base frequency
clang.toneA = 0.6f;       // First oscillator level
clang.toneB = 0.4f;       // Second oscillator level
clang.ratio = 1.47f;      // Inharmonic ratio (classic cowbell)
```

### DrumStack
```cpp
stack.mix1 = 0.8f;        // Layer 1 mix
stack.mix2 = 0.5f;        // Layer 2 mix
stack.mix3 = 0.0f;        // Layer 3 mix
```
