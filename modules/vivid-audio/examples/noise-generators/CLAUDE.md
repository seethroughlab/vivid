# Noise Generators

Demonstrates noise synthesis for audio textures and percussion.

## Operators Used

- **NoiseGen** - Colored noise generator (white, pink, brown)
- **Crackle** - Random impulse generator for vinyl texture
- **AudioFilter** - Shape noise spectrum
- **Decay** - Percussive envelope for triggered sounds
- **Clock** - Rhythm timing
- **AudioMixer** - Combine audio sources

## Key Concepts

### Noise Colors

Different noise colors have different frequency distributions:

```cpp
// White noise - equal energy per frequency (bright, harsh)
auto& white = chain.add<NoiseGen>("white");
white.setColor(NoiseColor::White);
white.volume = 0.5f;

// Pink noise - equal energy per octave (natural, balanced)
auto& pink = chain.add<NoiseGen>("pink");
pink.setColor(NoiseColor::Pink);

// Brown noise - -6dB/octave rolloff (deep, rumbling)
auto& brown = chain.add<NoiseGen>("brown");
brown.setColor(NoiseColor::Brown);
```

**Use cases:**
- White: Hi-hats, cymbals, harsh textures
- Pink: Wind, ocean, natural ambience
- Brown: Thunder, rumble, bass textures

### Filtered Noise for Percussion

Shape noise into hi-hat sounds with filtering and envelopes:

```cpp
// Noise source
auto& noise = chain.add<NoiseGen>("noise");
noise.setColor(NoiseColor::White);
noise.volume = 1.0f;

// Highpass filter for metallic character
auto& filter = chain.add<AudioFilter>("filter");
filter.setInput(&noise);
filter.setHighpass(8000.0f);
filter.resonance = 1.5f;

// Short decay envelope
auto& env = chain.add<Decay>("env");
env.setInput(&filter);
env.time = 0.05f;  // 50ms decay
env.setCurve(DecayCurve::Exponential);

// Trigger on beat
if (clock.triggered()) {
    env.trigger();
}
```

### Crackle (Vinyl Texture)

Generate random clicks and pops:

```cpp
auto& crackle = chain.add<Crackle>("crackle");
crackle.density = 0.0008f;  // Probability per sample
crackle.volume = 0.15f;     // Subtle level
```

**Density guide:**
- `0.0001f` - Very sparse, occasional pop
- `0.0005f` - Light vinyl crackle
- `0.002f` - Heavy crackle
- `0.01f` - Constant noise/glitch

### Layering Noise Sources

Combine multiple noise types:

```cpp
auto& mixer = chain.add<AudioMixer>("mixer");
mixer.addInput(&filtered_noise, 0.4f);  // Percussion
mixer.addInput(&crackle, 1.0f);         // Texture layer

auto& output = chain.add<AudioOutput>("audio_out");
output.input("mixer");
```

## Controls

- **Mouse X** - Select noise color (left=white, center=pink, right=brown)
- **Mouse Y** - Control volume (top=loud, bottom=quiet)
- Hi-hat triggers automatically on 8th notes

## Common Recipes

### Snare Body
```cpp
auto& noise = chain.add<NoiseGen>("snare_noise");
noise.setColor(NoiseColor::White);

auto& filter = chain.add<AudioFilter>("snare_filt");
filter.setInput(&noise);
filter.setType(FilterType::Bandpass);
filter.cutoff = 2000.0f;
filter.resonance = 1.0f;

auto& env = chain.add<Decay>("snare_env");
env.setInput(&filter);
env.time = 0.15f;
```

### Ocean Ambience
```cpp
auto& noise = chain.add<NoiseGen>("ocean");
noise.setColor(NoiseColor::Pink);
noise.volume = 0.3f;

auto& filter = chain.add<AudioFilter>("ocean_filt");
filter.setInput(&noise);
filter.setLowpass(800.0f);
```

### Vinyl Record Texture
```cpp
auto& crackle = chain.add<Crackle>("vinyl");
crackle.density = 0.0005f;
crackle.volume = 0.1f;

// Add subtle rumble
auto& rumble = chain.add<NoiseGen>("rumble");
rumble.setColor(NoiseColor::Brown);
rumble.volume = 0.05f;
```

## Related Operators

- **TapeEffect** - Tape saturation and wow/flutter
- **Bitcrush** - Lo-fi bit reduction
- **Oscillator** - Tonal synthesis
- **Kick** / **Snare** / **HiHat** - Complete drum voices
