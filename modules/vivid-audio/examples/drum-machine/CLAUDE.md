# Drum Machine

Demonstrates audio synthesis with drum operators and step sequencing.

## Operators Used

- **Clock** - Master timing with BPM and swing
- **Sequencer** - Pattern-based step sequencing
- **Euclidean** - Algorithmic rhythm generation
- **Kick** - 808-style kick drum synthesis
- **Snare** - Snare drum with tone/noise mix
- **HiHat** - Metallic hi-hat synthesis
- **Clap** - Hand clap with spread
- **AudioMixer** - Mix multiple audio sources
- **AudioOutput** - Play audio to speakers

## Key Concepts

### Clock - Master Timing
```cpp
auto& clock = chain.add<Clock>("clock");
clock.bpm(120.0f);
clock.division(ClockDiv::Sixteenth);  // 16th notes
clock.swing(0.0f);  // 0-1, shuffle amount

clock.start();
clock.stop();

// Check for trigger in update()
if (clock.triggered()) {
    // New step - trigger drums
}
```

### Sequencer - Pattern Sequencing
```cpp
auto& seq = chain.add<Sequencer>("seq");

// Set 16-step pattern as bitmask (bit 0 = step 1)
seq.setPattern(0x1111);  // X...X...X...X...
seq.setPattern(0x0404);  // ....X.......X...
seq.setPattern(0xFFFF);  // XXXXXXXXXXXXXXXX

// Step control
seq.advance();           // Move to next step
bool hit = seq.triggered(); // Check if current step is active
seq.reset();             // Back to step 1
```

### Euclidean - Algorithmic Rhythms
```cpp
auto& eucl = chain.add<Euclidean>("eucl");
eucl.steps(16);      // Total steps in pattern
eucl.hits(4);        // Number of active steps (evenly distributed)
eucl.rotation(0);    // Rotate pattern start position

eucl.advance();
bool hit = eucl.triggered();
```

### Drum Synthesis

**Kick Drum (808-style):**
```cpp
auto& kick = chain.add<Kick>("kick");
kick.pitch(50.0f);      // Base frequency
kick.pitchEnv(120.0f);  // Pitch sweep start
kick.pitchDecay(0.08f); // Pitch sweep time
kick.decay(0.4f);       // Amplitude decay
kick.click(0.4f);       // Attack click amount
kick.drive(0.2f);       // Distortion
kick.volume(0.9f);

kick.trigger();  // Play drum
```

**Snare Drum:**
```cpp
auto& snare = chain.add<Snare>("snare");
snare.tone(0.4f);       // Tone/noise balance
snare.noise(0.7f);      // Noise amount
snare.pitch(180.0f);    // Tone frequency
snare.toneDecay(0.08f); // Tone decay
snare.noiseDecay(0.15f);// Noise decay
snare.snappy(0.6f);     // High-frequency snap
snare.volume(0.7f);
```

**Hi-Hat:**
```cpp
auto& hihat = chain.add<HiHat>("hihat");
hihat.decay(0.05f);  // Short = closed, long = open
hihat.tone(0.7f);    // Metallic tone
hihat.ring(0.4f);    // Ring amount
hihat.volume(0.4f);
```

**Hand Clap:**
```cpp
auto& clap = chain.add<Clap>("clap");
clap.decay(0.25f);  // Overall decay
clap.tone(0.5f);    // Tone amount
clap.spread(0.6f);  // Stereo spread
clap.volume(0.5f);
```

### Audio Mixing
```cpp
auto& mixer = chain.add<AudioMixer>("mixer");
mixer.input(0, "kick").gain(0, 1.0f);
mixer.input(1, "snare").gain(1, 0.8f);
mixer.input(2, "hihat").gain(2, 0.5f);
mixer.input(3, "clap").gain(3, 0.6f);
mixer.volume(0.8f);
```

## Common Patterns

### Four-on-the-Floor
```cpp
kickSeq.setPattern(0x1111);   // X...X...X...X...
snareSeq.setPattern(0x0404);  // ....X.......X... (backbeat)
hihatSeq.setPattern(0x5555);  // X.X.X.X.X.X.X.X. (8ths)
```

### Breakbeat
```cpp
kickSeq.setPattern(0x1199);   // X...X..XX...X..X
snareSeq.setPattern(0x0C0C);  // ....XX......XX..
hihatSeq.setPattern(0xFFFF);  // 16th notes
```

### Euclidean Polyrhythm
```cpp
kickEucl.steps(16).hits(4);   // 4 even kicks
snareEucl.steps(16).hits(5);  // 5 snares
hihatEucl.steps(16).hits(7);  // 7 hi-hats
```

## Controls

- **SPACE**: Start/Stop
- **1-4**: Trigger drums manually (Kick, Snare, HiHat, Clap)
- **UP/DOWN**: Adjust BPM (+/-5)
- **LEFT/RIGHT**: Change pattern preset
- **E**: Toggle Euclidean mode
- **S**: Cycle swing amount
- **TAB**: Open parameter controls
