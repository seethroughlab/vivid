# Lo-Fi Textures Example

Demonstrates vintage audio degradation effects for lo-fi aesthetics.

## Operators Demonstrated

- **Crackle** - Random impulse generator for vinyl texture
- **TapeEffect** - Tape saturation, wow/flutter, hiss
- **Bitcrush** - Bit depth and sample rate reduction

## Key Concepts

### Crackle (Vinyl Texture)
```cpp
auto& crackle = chain.add<Crackle>("crackle");
crackle.density = 0.0005f;   // Probability per sample (sparse)
crackle.volume = 0.08f;      // Subtle amplitude
```

Density guide:
- `0.0001f` - Very sparse, occasional pop
- `0.0005f` - Light vinyl crackle
- `0.002f` - Heavy crackle
- `0.01f` - Constant noise (glitch effect)

### TapeEffect
```cpp
auto& tape = chain.add<TapeEffect>("tape");
tape.setInput(&source);

// Saturation (warmth/compression)
tape.saturation = 0.4f;      // 0 = clean, 1 = heavy

// Wow (slow pitch drift)
tape.wowDepth = 0.002f;      // Pitch deviation amount
tape.wowRate = 0.5f;         // Hz (slow oscillation)

// Flutter (fast micro-variations)
tape.flutterDepth = 0.001f;
tape.flutterRate = 6.0f;     // Hz

// Tape hiss
tape.hissLevel = 0.05f;      // Background noise
```

### Bitcrush
```cpp
auto& crush = chain.add<Bitcrush>("crush");
crush.setInput(&source);
crush.bits = 12;          // Bit depth (1-16, 16 = CD quality)
crush.downsample = 2;     // Sample rate divisor
crush.mix = 0.5f;         // Wet/dry blend
```

Bit depth effects:
- `16` - Clean (CD quality)
- `12` - Subtle grit
- `8` - Retro game console
- `4` - Heavy distortion
- `1` - Square wave

### Layering Lo-Fi Effects

Chain order matters:
```cpp
// Source → Tape → Bitcrush → Mixer (with Crackle)
pad → tape → crush ─┐
                    ├─→ mixer → output
crackle ────────────┘
```

### Mixing Crackle
```cpp
auto& mixer = chain.add<AudioMixer>("mix");
mixer.addInput(&processedAudio, 1.0f);
mixer.addInput(&crackle, 1.0f);  // Add on top
```

## Visual Pairing

Lo-fi audio pairs well with:
```cpp
// Film grain overlay
chain.add<FilmGrain>("grain");
grain.intensity = 0.15f;
grain.size = 1.5f;

// High-frequency noise texture
chain.add<Noise>("noise");
noise.scale = 100.0f;  // Fine grain
noise.octaves = 1;
```

## Related Operators

- **NoiseGen** - White/pink noise generation
- **AudioFilter** - Frequency shaping
- **Overdrive** - Distortion/saturation
- **Phaser** / **Flanger** - Modulation effects
