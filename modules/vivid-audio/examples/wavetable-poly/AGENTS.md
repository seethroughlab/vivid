# Wavetable Polyphonic Synthesis Example

Demonstrates morphing wavetable synthesis with polyphony.

## Operators Demonstrated

- **WavetableSynth** - Polyphonic wavetable synth with morphing
- **LFO** - Low frequency oscillator for modulation
- **Chorus** / **Reverb** - Audio effects

## Key Concepts

### Basic Wavetable Setup
```cpp
auto& wt = chain.add<WavetableSynth>("wt");
wt.loadBuiltin(BuiltinTable::Analog);  // Preset wavetable
wt.maxVoices = 4;                       // Polyphony limit
wt.position = 0.5f;                     // Morph position (0-1)
```

### Built-in Wavetables
```cpp
wt.loadBuiltin(BuiltinTable::Basic);    // Sine→Tri→Saw→Square
wt.loadBuiltin(BuiltinTable::Analog);   // Warm, detuned classics
wt.loadBuiltin(BuiltinTable::Digital);  // FM-like, harsh
wt.loadBuiltin(BuiltinTable::Vocal);    // Vowel formants (A-E-I-O-U)
wt.loadBuiltin(BuiltinTable::Texture);  // Granular, organic
wt.loadBuiltin(BuiltinTable::PWM);      // Pulse width sweep
```

### Amplitude Envelope (ADSR)
```cpp
wt.attack = 0.05f;   // Attack time (seconds)
wt.decay = 0.2f;     // Decay time
wt.sustain = 0.6f;   // Sustain level (0-1)
wt.release = 0.5f;   // Release time
```

### Filter with Envelope
```cpp
wt.filterCutoff = 2000.0f;           // Base cutoff Hz
wt.filterResonance = 0.3f;           // Q factor (0-1)
wt.setFilterType(SynthFilterType::LP24);  // 24dB/oct lowpass

// Filter envelope
wt.filterAttack = 0.01f;
wt.filterDecay = 0.3f;
wt.filterSustain = 0.2f;
wt.filterRelease = 0.4f;
wt.filterEnvAmount = 0.6f;  // How much env affects cutoff
```

### Unison for Thickness
```cpp
wt.unisonVoices = 3;        // Voices per note
wt.unisonSpread = 15.0f;    // Detune in cents
wt.unisonStereo = 0.8f;     // Stereo width (0=mono)
```

### Warp Modes
Phase warping adds timbral variety:
```cpp
wt.setWarpMode(WarpMode::None);      // Clean (default)
wt.setWarpMode(WarpMode::Sync);      // Hard sync
wt.setWarpMode(WarpMode::BendPlus);  // Brighter
wt.setWarpMode(WarpMode::FM);        // Self-modulation
wt.warpAmount = 0.5f;                // Intensity (0-1)
```

### Note Control
```cpp
// By frequency
wt.noteOn(440.0f, 0.8f);   // A4, velocity 0.8
wt.noteOff(440.0f);

// By MIDI note
wt.noteOnMidi(60, 100);    // Middle C, velocity 100
wt.noteOffMidi(60);

// Utilities
wt.allNotesOff();          // Release all
wt.panic();                // Immediate silence
int count = wt.activeVoiceCount();
```

### Position Modulation
Animate the wavetable position for evolving timbre:
```cpp
// With LFO
wt.position = 0.5f + 0.4f * lfo.value();

// Manual
wt.position = 0.5f + 0.3f * std::sin(time * 0.5f);
```

## Related Operators

- **FMSynth** - FM synthesis
- **Synth** - Simple oscillator synth
- **MidiIn** - Control from MIDI keyboard
- **Envelope** - Standalone envelope generator
