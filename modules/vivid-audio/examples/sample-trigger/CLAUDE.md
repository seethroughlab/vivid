# Sample Trigger

Demonstrates sample loading and playback with pitch control.

## Operators Used

- **SampleBank** - Load audio samples from folder
- **SamplePlayer** - Polyphonic sample playback with pitch
- **Reverb** - Spatial reverb effect
- **AudioGain** - Volume control

## Key Concepts

### Loading Samples
```cpp
// Load from folder (all .wav files)
auto& bank = chain.add<SampleBank>("bank");
bank.folder("assets/audio/samples");

// Or load individual files
bank.file("assets/audio/kick.wav")
    .file("assets/audio/snare.wav")
    .file("assets/audio/hihat.wav");

// Get sample names
auto names = bank.names();
for (const auto& name : names) {
    std::cout << name << std::endl;
}
```

### Sample Playback
```cpp
auto& player = chain.add<SamplePlayer>("player");
player.bank("bank");     // Connect to SampleBank
player.voices(16);       // Max simultaneous voices
player.volume(0.8f);

// Trigger sample by index
player.trigger(0);  // First sample

// Trigger with parameters
player.trigger(
    0,      // Sample index
    0.8f,   // Velocity (0-1)
    0.0f,   // Pan (-1 to 1)
    1.0f    // Pitch (1.0 = normal, 2.0 = octave up, 0.5 = octave down)
);
```

### Pitch Variation
```cpp
// Normal pitch
player.trigger(i, 1.0f, 0.0f, 1.0f);

// Pitch up (1.0 to 2.0)
float pitchUp = 1.0f + (rand() / (float)RAND_MAX) * 1.0f;
player.trigger(i, 0.8f, 0.0f, pitchUp);

// Pitch down (0.5 to 1.0)
float pitchDown = 0.5f + (rand() / (float)RAND_MAX) * 0.5f;
player.trigger(i, 0.9f, 0.0f, pitchDown);
```

### Effects Chain
```cpp
// Reverb for ambience
auto& reverb = chain.add<Reverb>("reverb");
reverb.input("player");
reverb.roomSize(0.4f);
reverb.damping(0.5f);
reverb.mix(0.2f);

// Master gain
auto& gain = chain.add<AudioGain>("gain");
gain.input("reverb");
gain.gain(1.0f);
```

## Audio Routing
```
SampleBank ─► SamplePlayer ─► Reverb ─► AudioGain ─► AudioOutput
```

## File Format Support

- WAV (16-bit, 24-bit, 32-bit float)
- Sample rate conversion handled automatically
- Stereo and mono supported

## Controls

- **1-8**: Trigger samples (normal pitch)
- **Q-I**: Trigger samples (pitch up, random)
- **A-K**: Trigger samples (pitch down, random)
- **UP/DOWN**: Master volume
- **TAB**: Open parameter controls

Place .wav files in `assets/audio/samples/` folder.
