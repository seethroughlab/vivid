# Sampler Basics

Demonstrates sample playback with three sampler types.

## Sample Requirements

This example needs audio samples in the `assets/` folder:

### For Sampler (Demo 1)
- `assets/audio/piano_c4.wav` - A single piano note (C4/middle C)

### For SamplePlayer (Demo 2)
- `assets/audio/drums/` folder containing WAV files:
  - kick.wav
  - snare.wav
  - hihat.wav
  - clap.wav
  - etc.

### For MultiSampler (Demo 3)
- `assets/sample_packs/Piano/preset.json` - A JSON preset file
- Sample WAV files referenced by the preset

## Operators Used

- **Sampler** - Chromatic sampler (like Ableton Simpler)
- **SamplePlayer** - Trigger samples from a bank
- **MultiSampler** - Multi-zone sampler with velocity layers
- **SampleBank** - Collection of samples for SamplePlayer

## Key Concepts

### Sampler (Chromatic Playback)

Load one sample and play it at different pitches:
```cpp
auto& sampler = chain.add<Sampler>("sampler");
sampler.loadSample("assets/audio/piano_c4.wav");
sampler.rootNote = 60;  // Original pitch is C4

// Envelope settings
sampler.attack = 0.01f;
sampler.decay = 0.1f;
sampler.sustain = 0.8f;
sampler.release = 0.5f;

// Play notes
sampler.noteOn(60, 0.8f);   // C4 at 80% velocity
sampler.noteOn(64, 0.6f);   // E4 at 60% velocity
sampler.noteOff(60);        // Release C4
sampler.allNotesOff();      // Release all
```

### SamplePlayer (Drum Machine Style)

Trigger one-shot samples by index or name:
```cpp
// Bank loads all WAV files from a folder
auto& bank = chain.add<SampleBank>("bank");
bank.folder("assets/audio/drums");

auto& player = chain.add<SamplePlayer>("player");
player.setBank("bank");
player.setVoices(8);

// Trigger by index
player.trigger(0);                    // First sample
player.trigger(1, 0.8f);              // With volume
player.trigger(2, 0.8f, -0.5f);       // With pan
player.trigger(3, 1.0f, 0.0f, 2.0f);  // With pitch (octave up)

// Trigger by name
player.trigger("kick");
player.trigger("snare", 0.9f);

// Looped playback
int voiceId = player.triggerLoop("pad");
player.stop(voiceId);

player.stopAll();
```

### MultiSampler (Kontakt Style)

Load sample libraries with key zones and velocity layers:
```cpp
auto& multi = chain.add<MultiSampler>("multi");

// Load from JSON preset
multi.loadPreset("assets/sample_packs/Piano/preset.json");

// Or load Decent Sampler format directly
multi.loadDspreset("assets/sample_packs/Strings.dspreset");

// Or add regions manually
SampleRegion region;
region.path = "samples/c4.wav";
region.rootNote = 60;
region.loNote = 58;
region.hiNote = 62;
region.loVel = 0;
region.hiVel = 64;   // Soft layer
multi.addRegion(region);

// Play
multi.noteOn(60, 0.5f);  // Will select appropriate sample
multi.noteOff(60);

// Velocity curve (-1=soft, 0=linear, 1=hard)
multi.velCurve = -0.5f;  // More sensitive to soft playing
```

## JSON Preset Format

```json
{
  "name": "Grand Piano",
  "samples": [
    {
      "path": "Samples/C4_soft.wav",
      "root_note": 60,
      "lo_note": 58,
      "hi_note": 62,
      "lo_vel": 0,
      "hi_vel": 64,
      "volume_db": 0
    },
    {
      "path": "Samples/C4_loud.wav",
      "root_note": 60,
      "lo_note": 58,
      "hi_note": 62,
      "lo_vel": 65,
      "hi_vel": 127,
      "volume_db": -3
    }
  ],
  "envelope": {
    "attack": 0.01,
    "decay": 0.1,
    "sustain": 0.8,
    "release": 0.5
  }
}
```

## Sample Requirements

### WAV Format
- 16-bit or 24-bit
- 44.1kHz or 48kHz recommended
- Mono or stereo

### Good Samples For...

**Sampler (chromatic):**
- Single sustained notes
- Loopable sounds
- Samples at C4 (middle C) work best

**SamplePlayer (one-shots):**
- Drum hits
- Sound effects
- Vocal chops

**MultiSampler (realistic instruments):**
- Multiple velocity layers
- Multiple key zones
- Sample libraries (Decent Sampler, etc.)

## Controls

- **Keys 1/2/3** - Switch demo mode
- **QWERTY row** - Play notes (Q=C4, W=D4, E=E4, etc.)
- **ZXCV row** - Lower octave
- **Mouse Y** - Velocity

## Voice Management

```cpp
// Check active voices
int active = sampler.activeVoiceCount();

// Voice stealing (when max voices exceeded)
sampler.setVoiceStealMode(SamplerVoiceStealMode::Oldest);
sampler.setVoiceStealMode(SamplerVoiceStealMode::Quietest);
sampler.setVoiceStealMode(SamplerVoiceStealMode::None);

// Emergency stop
sampler.panic();  // Immediate silence
```

## Tips

1. Set `rootNote` to match the sample's actual pitch
2. Use short attacks for plucky sounds
3. Use velocity layers for expressive playing
4. SamplePlayer is best for drums (no pitch tracking)
5. MultiSampler needs more CPU but sounds more realistic
