# Drum Synthesis

808-style drum machine with step sequencer.

## Operators Used

- **Kick** - 808-style kick drum with pitch envelope
- **Snare** - Snare drum with noise component
- **HiHat** - Metallic hi-hat
- **Clap** - Hand clap with spread
- **Clock** - Master tempo with swing
- **Sequencer** - 16-step pattern sequencer
- **AudioMixer** - Combine drum voices
- **AudioOutput** - Send to speakers

## Key Concepts

### Master Clock
Sets tempo and triggers sequencers:
```cpp
auto& clock = chain.add<Clock>("clock");
clock.bpm = 120.0f;
clock.division(ClockDiv::Sixteenth);  // 16th notes
clock.swing = 0.1f;                   // Slight swing feel
clock.start();
```

### Drum Voices
Each drum is a self-contained synthesizer:
```cpp
// Kick - pitch envelope creates the "boom"
auto& kick = chain.add<Kick>("kick");
kick.pitch = 50.0f;        // Base frequency
kick.pitchEnv = 150.0f;    // Pitch sweep amount
kick.decay = 0.5f;         // Length
kick.click = 0.4f;         // Attack transient

// Snare - tone + noise
auto& snare = chain.add<Snare>("snare");
snare.tone = 180.0f;       // Body frequency
snare.snappy = 0.7f;       // Snare wire noise

// HiHat - filtered noise
auto& hihat = chain.add<HiHat>("hihat");
hihat.decay = 0.05f;       // Closed hat (short)
hihat.tone = 0.3f;         // Metallic character

// Clap - multiple hits
auto& clap = chain.add<Clap>("clap");
clap.spread = 0.03f;       // Timing variation
```

### Step Sequencer
16-step pattern with velocity:
```cpp
auto& seq = chain.add<Sequencer>("seq");
seq.steps = 16;

// Four-on-the-floor pattern
seq.setStep(0, true);   // Beat 1
seq.setStep(4, true);   // Beat 2
seq.setStep(8, true);   // Beat 3
seq.setStep(12, true);  // Beat 4

// With velocity
seq.setStep(0, true, 1.0f);   // Full velocity
seq.setStep(2, true, 0.6f);   // Softer ghost note
```

### Pattern from Bitmask
Quick pattern setup:
```cpp
// Binary: 1001001001001001 = kick on 1,5,9,13
seq.setPattern(0b1001001001001001);
```

### Update Loop
Advance sequencers and trigger drums:
```cpp
if (clock.triggered()) {
    kick_seq.advance();
    snare_seq.advance();

    if (kick_seq.triggered()) {
        kick.trigger();
    }
    if (snare_seq.triggered()) {
        snare.trigger();
    }
}
```

### Audio Mixing
Combine drum voices:
```cpp
auto& mixer = chain.add<AudioMixer>("mixer");
mixer.addInput(&kick);
mixer.addInput(&snare);
mixer.addInput(&hihat);
mixer.addInput(&clap);

auto& output = chain.add<AudioOutput>("audio_out");
output.input("mixer");
```

## Controls

- **Mouse X** - BPM (80-160)
- **Mouse Y** - Swing amount (0-0.5)

## Common Patterns

### Four-on-the-Floor
```cpp
kick_seq.setPattern(0b1000100010001000);
snare_seq.setPattern(0b0000100000001000);
hihat_seq.setPattern(0b1111111111111111);
```

### Hip-Hop
```cpp
kick_seq.setPattern(0b1000001010001010);
snare_seq.setPattern(0b0000100000001000);
hihat_seq.setPattern(0b1010101010101010);
```

### Breakbeat
```cpp
kick_seq.setPattern(0b1000000110000010);
snare_seq.setPattern(0b0000100100001001);
hihat_seq.setPattern(0b1010101010101010);
```

## Visual Feedback
Shapes pulse with drum envelopes:
```cpp
float kickEnv = kick.ampEnvelope();  // 0-1
float kickSize = 0.08f + kickEnv * 0.15f;
kick_viz.size.set(kickSize, kickSize);
```
