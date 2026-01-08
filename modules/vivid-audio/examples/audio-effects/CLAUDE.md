# Audio Effects

Demonstrates audio effect processing chains.

## Operators Used

- **Compressor** - Dynamic range control
- **Phaser** - Sweeping notch filter
- **Flanger** - Short modulated delay
- **Bitcrush** - Lo-fi bit/sample reduction
- **Overdrive** - Soft clipping saturation
- **Synth** - Audio source
- **AudioMixer** - Mix multiple sources

## Key Concepts

### Compressor
Reduces dynamic range:
```cpp
auto& comp = chain.add<Compressor>("comp");
comp.input("audio");
comp.threshold = -18.0f;   // Compress above this level (dB)
comp.ratio = 4.0f;         // 4:1 compression
comp.attack = 5.0f;        // 5ms attack
comp.release = 80.0f;      // 80ms release
comp.makeupGain = 6.0f;    // +6dB output boost
comp.knee = 3.0f;          // Soft knee (dB)
```

### Phaser
Sweeping all-pass filters:
```cpp
auto& phaser = chain.add<Phaser>("phaser");
phaser.input("audio");
phaser.rate = 0.4f;        // LFO rate (Hz)
phaser.depth = 0.9f;       // Modulation depth
phaser.stages = 8;         // All-pass stages (more = more notches)
phaser.feedback = 0.6f;    // Resonance
phaser.mix = 0.5f;         // Dry/wet
```

### Flanger
Short modulated delay (jet/whoosh):
```cpp
auto& flanger = chain.add<Flanger>("flanger");
flanger.input("audio");
flanger.rate = 0.25f;      // LFO rate (Hz)
flanger.depth = 0.8f;      // Modulation depth
flanger.feedback = 0.7f;   // High = more metallic
flanger.mix = 0.5f;
```

### Bitcrush
Lo-fi digital degradation:
```cpp
auto& bitcrush = chain.add<Bitcrush>("bitcrush");
bitcrush.input("audio");
bitcrush.bits = 8;              // Bit depth (2-16)
bitcrush.sampleRate = 12000.0f; // Downsample rate
bitcrush.mix = 1.0f;
```

### Overdrive
Soft clipping saturation:
```cpp
auto& overdrive = chain.add<Overdrive>("overdrive");
overdrive.input("audio");
overdrive.drive = 0.7f;    // Drive amount (0-1)
overdrive.tone = 0.6f;     // Tone shaping (0=dark, 1=bright)
overdrive.mix = 0.8f;      // Dry/wet
```

### Effect Chaining
Effects can be chained:
```cpp
comp.input("synth");
phaser.input("compressor");    // Compress first
flanger.input("phaser");       // Then phase
// etc.
```

### Parallel Effects
Use AudioMixer for parallel processing:
```cpp
auto& mixer = chain.add<AudioMixer>("mixer");
mixer.addInput(&comp, 0.5f);     // 50% compressed
mixer.addInput(&phaser, 0.3f);   // 30% phased
mixer.addInput(&synth, 0.2f);    // 20% dry
```

## Controls

- **Mouse X** - Select effect (0-4: Compressor, Phaser, Flanger, Bitcrush, Overdrive)
- **Mouse Y** - Adjust effect intensity parameter

## Common Settings

### Punchy Compression (drums)
```cpp
comp.threshold = -12.0f;
comp.ratio = 6.0f;
comp.attack = 1.0f;     // Fast attack catches transients
comp.release = 50.0f;
```

### Vocal Compression
```cpp
comp.threshold = -20.0f;
comp.ratio = 3.0f;
comp.attack = 10.0f;    // Slower to preserve dynamics
comp.release = 150.0f;
comp.knee = 6.0f;       // Soft knee for natural sound
```

### Classic Phaser
```cpp
phaser.rate = 0.3f;
phaser.depth = 0.8f;
phaser.stages = 6;
phaser.feedback = 0.5f;
```

### Jet Flanger
```cpp
flanger.rate = 0.1f;
flanger.depth = 1.0f;
flanger.feedback = 0.9f;  // Very high feedback
```

### 8-bit Retro
```cpp
bitcrush.bits = 8;
bitcrush.sampleRate = 22050.0f;
```

### Tube-style Overdrive
```cpp
overdrive.drive = 0.5f;
overdrive.tone = 0.4f;  // Warm, not harsh
```
