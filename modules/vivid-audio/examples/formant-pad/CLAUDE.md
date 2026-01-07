# Formant Pad

Demonstrates vocal synthesis with formant filtering and polyphonic oscillators.

## Operators Used

- **Oscillator** - Detuned saw waves for rich pad sound
- **Envelope** - ADSR amplitude envelope
- **Formant** - Vocal formant filter (vowel sounds)
- **Reverb** - Spatial reverb effect
- **AudioMixer** - Mix multiple oscillators
- **AudioGain** - Envelope-controlled amplitude
- **Levels** - Audio level analysis for visuals

## Key Concepts

### Polyphonic Pad Sound
```cpp
// Multiple detuned oscillators for thick pad
auto& osc1 = chain.add<Oscillator>("osc1");
osc1.frequency(freq).waveform(Waveform::Saw).volume(0.25f);

auto& osc2 = chain.add<Oscillator>("osc2");
osc2.frequency(freq * 1.005f).waveform(Waveform::Saw).volume(0.2f); // Slightly sharp

auto& osc3 = chain.add<Oscillator>("osc3");
osc3.frequency(freq * 0.995f).waveform(Waveform::Saw).volume(0.2f); // Slightly flat

auto& sub = chain.add<Oscillator>("sub");
sub.frequency(freq * 0.5f).waveform(Waveform::Sine).volume(0.15f); // Sub octave
```

### ADSR Envelope
```cpp
auto& env = chain.add<Envelope>("env");
env.attack(0.15f);   // Time to reach peak
env.decay(0.2f);     // Time to reach sustain
env.sustain(0.7f);   // Sustain level (0-1)
env.release(0.8f);   // Time to fade out

// Trigger on note
env.trigger();
```

### Formant Filter (Vowel Sounds)
```cpp
auto& formant = chain.add<Formant>("formant");
formant.input("enveloped");
formant.resonance(8.0f);  // Filter sharpness (1-20)
formant.mix(1.0f);        // Wet/dry mix

// Vowel selection
formant.vowel(Vowel::A);  // "ah"
formant.vowel(Vowel::E);  // "eh"
formant.vowel(Vowel::I);  // "ee"
formant.vowel(Vowel::O);  // "oh"
formant.vowel(Vowel::U);  // "oo"
```

### Frequency Constants
```cpp
// Use built-in frequency constants
float freq = freq::A4;   // 440 Hz
float freq = freq::C4;   // Middle C
float freq = freq::D3;   // D below middle C

// D minor scale
const float D_MINOR[] = {
    freq::D3, freq::E3, freq::F3, freq::G3,
    freq::A3, freq::Bb3, freq::C4, freq::D4
};
```

### Reverb
```cpp
auto& reverb = chain.add<Reverb>("reverb");
reverb.input("formant");
reverb.roomSize(0.85f);  // Room size (0-1)
reverb.damping(0.4f);    // High-frequency damping
reverb.mix(0.4f);        // Wet/dry mix
```

### Envelope-Controlled Gain
```cpp
// Apply envelope to audio using AudioGain with gainInput
auto& enveloped = chain.add<AudioGain>("enveloped");
enveloped.input("osc_mix");
enveloped.gainInput("env");  // Envelope controls volume
```

## Audio Routing
```
osc1 ─┐
osc2 ─┼─► AudioMixer ─► AudioGain ─► Formant ─► Reverb ─► AudioOutput
osc3 ─┤        ▲
sub  ─┘        │
               │
           Envelope
```

## Controls

- **A S D F G H J K**: Play D minor scale (D3 to D4)
- **UP/DOWN**: Adjust formant resonance
- **LEFT/RIGHT**: Adjust reverb mix
- **F**: Toggle fullscreen

Vowel changes randomly with each key press.
