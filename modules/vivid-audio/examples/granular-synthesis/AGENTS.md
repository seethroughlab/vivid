# Granular Synthesis Example

Demonstrates grain-based audio processing for textural soundscapes.

## Operators Demonstrated

- **Granular** - Granular synthesizer for time-stretching and texture
- **AudioFile** - Load audio files for processing
- **Reverb** - Add spatial depth

## Key Concepts

### Granular Synthesis
Granular synthesis splits audio into tiny grains (10-500ms) and recombines them with randomization:

```cpp
auto& grain = chain.add<Granular>("clouds");
grain.loadSample("assets/audio/texture.wav");

// Grain parameters
grain.grainSize = 80.0f;      // Grain duration in ms
grain.density = 15.0f;        // Grains per second
grain.position = 0.5f;        // Playhead position (0-1)
grain.positionSpray = 0.1f;   // Random position variation

// Pitch control
grain.pitch = 1.0f;           // Pitch multiplier (0.25-4)
grain.pitchSpray = 0.05f;     // Random pitch variation

// Stereo
grain.panSpray = 0.3f;        // Random stereo spread
```

### Grain Window Shapes
```cpp
grain.setWindow(GrainWindow::Hann);      // Smooth (default)
grain.setWindow(GrainWindow::Triangle);  // Linear
grain.setWindow(GrainWindow::Gaussian);  // Soft, diffuse
grain.setWindow(GrainWindow::Tukey);     // Flat middle
grain.setWindow(GrainWindow::Rectangle); // No fade (harsh)
```

### Freeze Mode
Hold the playhead position for sustained drones:
```cpp
grain.setFreeze(true);   // Position stays fixed, only spray varies
grain.setFreeze(false);  // Normal playback
```

### Loading Audio
```cpp
// From file
grain.loadSample("path/to/audio.wav");

// Check status
if (grain.isLoaded()) {
    float duration = grain.sampleDuration();
}
```

## Typical Parameter Ranges

| Parameter | Short Grains | Long Grains | Freeze/Drone |
|-----------|--------------|-------------|--------------|
| grainSize | 10-50ms | 100-300ms | 200-500ms |
| density | 20-50 | 5-15 | 8-20 |
| positionSpray | 0.02-0.05 | 0.1-0.2 | 0.3-0.5 |
| pitchSpray | 0-0.02 | 0.05-0.1 | 0.1-0.3 |

## Related Operators

- **WavetableSynth** - Another approach to evolving timbres
- **Sampler** - Trigger-based sample playback
- **Delay** / **Reverb** - Add spatial processing
