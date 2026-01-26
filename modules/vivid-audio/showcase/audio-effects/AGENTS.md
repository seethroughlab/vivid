# Audio Effects

Demonstrates audio effect processing chains with real-time visualization.

## What This Example Shows

- **Effect selection**: Move mouse horizontally to switch between 5 different audio effects
- **Parameter control**: Move mouse vertically to adjust the active effect's main parameter
- **Visual feedback**: FFT spectrum analyzer shows how each effect changes the audio
- **Text labels**: Current effect name and parameter value displayed on screen

## Operators Used

### Audio
- **Synth** - Audio source (sawtooth oscillator)
- **Clock** - Triggers notes automatically
- **Compressor** - Dynamic range control
- **Phaser** - Sweeping notch filter
- **Flanger** - Short modulated delay
- **Bitcrush** - Lo-fi bit/sample reduction
- **Overdrive** - Soft clipping saturation
- **AudioMixer** - Route between effects
- **FFT** - Spectrum analysis

### Visual
- **Canvas** - 2D drawing for text and spectrum bars

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
bitcrush.bits = 8;                    // Bit depth (2-16)
bitcrush.targetSampleRate = 12000.0f; // Downsample rate
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
mixer.setInput(0, "compressor"); mixer.setGain(0, 0.5f);  // 50% compressed
mixer.setInput(1, "phaser");     mixer.setGain(1, 0.3f);  // 30% phased
mixer.setInput(2, "synth");      mixer.setGain(2, 0.2f);  // 20% dry
```

### FFT Spectrum Visualization
Analyze audio frequencies with FFT:
```cpp
auto& fft = chain.add<FFT>("fft");
fft.input("mixer");
fft.setSize(512);        // FFT size (power of 2)
fft.smoothing = 0.85f;   // Temporal smoothing (0-1)

// In update():
int binCount = fft.binCount();
for (int i = 0; i < 64; i++) {
    // Logarithmic mapping for better bass visibility
    int bin = (int)(pow((float)i / 64, 2.0) * binCount * 0.5);
    float magnitude = fft.bin(bin);  // 0-1 range
    // Draw bar with height = magnitude * maxHeight
}
```

### Canvas Text Rendering
Display text labels with Canvas:
```cpp
auto& canvas = chain.get<Canvas>("canvas");

// Load font
canvas.loadBuiltinFont(ctx, 14.0f);

// Set text style
canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
canvas.textAlign(TextAlign::Left);
canvas.textBaseline(TextBaseline::Top);

// Draw text (manually center for monospace: ~8.4px per char at 14px)
float textX = 640.0f - strlen(text) * 4.2f;
canvas.fillText("EFFECT NAME", textX, 40.0f);
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
bitcrush.targetSampleRate = 22050.0f;
```

### Tube-style Overdrive
```cpp
overdrive.drive = 0.5f;
overdrive.tone = 0.4f;  // Warm, not harsh
```
