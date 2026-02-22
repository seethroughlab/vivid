# Sampler Basics Example

Demonstrates sample playback with two different approaches.

## Operators Demonstrated

- **Sampler** - Polyphonic chromatic sample player with ADSR envelope
- **SampleBank** - Non-audio storage operator that loads WAV files into memory
- **SamplePlayer** - Plays samples from a connected SampleBank by index or name

## Key Concepts

### Sampler (Chromatic Playback)
Load a single WAV and play it at different pitches based on MIDI notes:

```cpp
auto& sampler = chain.add<Sampler>("piano");
sampler.loadSample("assets/piano_c4.wav");
sampler.rootNote = 60;       // MIDI note of the original sample pitch
sampler.maxVoices = 8;

// Built-in ADSR envelope
sampler.attack = 0.01f;
sampler.decay = 0.2f;
sampler.sustain = 0.8f;
sampler.release = 0.5f;

// Play notes
sampler.noteOn(60, 0.9f);   // Middle C at 90% velocity
sampler.noteOn(64, 0.7f);   // E4 (pitch-shifted from root)
sampler.noteOff(60);         // Release Middle C
```

### SampleBank + SamplePlayer (Multi-Sample Triggering)
Load a folder of WAVs and trigger them independently:

```cpp
// SampleBank: storage only, no audio output
auto& bank = chain.add<SampleBank>("drums");
bank.setFolder("assets/audio/drums");    // Load all WAVs
// bank.addFile("assets/single.wav");    // Or add individually

// SamplePlayer: plays from a connected bank
auto& player = chain.add<SamplePlayer>("player");
player.setBank("drums");
player.setVoices(8);

// Trigger by index
player.trigger(0);                          // Play first sample
player.trigger(1, 0.8f);                    // With volume
player.trigger(2, 0.9f, -0.5f);             // With pan (left)
player.trigger(3, 0.7f, 0.0f, 1.5f);        // With pitch (up)

// Trigger by name (filename without extension)
player.trigger("snare", 0.8f);

// Looped playback
int voice = player.triggerLoop(0, 0.5f);     // Returns voice ID
player.stop(voice);                          // Stop specific voice
player.stopAll();                            // Stop everything
```

### Voice Stealing
Sampler supports voice stealing when polyphony is exceeded:

```cpp
sampler.setVoiceStealMode(SamplerVoiceStealMode::Oldest);   // Replace oldest
sampler.setVoiceStealMode(SamplerVoiceStealMode::Quietest); // Replace quietest
sampler.setVoiceStealMode(SamplerVoiceStealMode::None);     // Ignore new notes
```

### Loop Points
```cpp
sampler.setLoop(true);
sampler.setLoopPoints(0.5f, 2.0f);  // Loop between 0.5s and 2.0s
```

## Related Operators

- **MultiSampler** - Full-featured multi-zone sampler with velocity layers (see multi-sampler example)
- **Granular** - Grain-based sample processing
- **AudioFile** - Simple audio file playback
