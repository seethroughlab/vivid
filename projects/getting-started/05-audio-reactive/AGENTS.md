# Lesson 5: Audio-Reactive

## Commands
- Run: `vivid .`
- Show UI: `vivid . --show-ui` (press Tab to toggle)

## Modules
- Audio: AudioIn, Levels, BandSplit, BeatDetect, FFT
- Core: Noise, HSV

## Lesson Focus
Audio analysis for driving visual parameters with amplitude and frequency data.

## Key Concepts
- **AudioIn**: Captures audio from microphone/line input
- **Levels**: RMS (average) and peak amplitude analysis
- **BandSplit**: Separates audio into frequency bands (bass, mid, high)
- **Smoothing**: Reduces jitter for smoother visual response

## Audio Operators Reference
| Operator | Purpose | Key Methods |
|----------|---------|-------------|
| `AudioIn` | Microphone capture | `.volume`, `.setMute()` |
| `Levels` | Amplitude | `.rms()`, `.peak()` |
| `BandSplit` | Frequency bands | `.bass()`, `.mid()`, `.high()` |
| `BeatDetect` | Beat/onset detection | `.beat()`, `.intensity()` |
| `FFT` | Full spectrum | `.bin(i)`, `.binCount()` |

## Alternative Approaches

Instead of always using Noise for audio-reactive visuals, consider:

| Operator | Use Case | Audio Mapping |
|----------|----------|---------------|
| **Shape** | Geometric primitives | Size ← bass, rotation ← time |
| **Ramp** | Smooth gradients | Radius ← bass, hue ← high |
| **Particles** | Point-based effects | Burst on beat, emit rate ← energy |
| **Flash** | Beat-synced intensity | Trigger on kick/snare |
| **Feedback** | Motion trails | Decay ← energy, zoom ← bass |

See lesson 11 (shapes-reactive) for a complete example without noise.

## Suggested Modifications

1. **Add beat detection**: `if (beat.beat()) { /* trigger */ }`

2. **Use different frequency bands**: `.subBass()`, `.lowMid()`, `.highMid()`

3. **Adjust smoothing**: 0.5 (punchy), 0.9 (smooth), 0.95 (ambient)

4. **Try shapes instead of noise**: Replace Noise with Shape for cleaner geometry

## Troubleshooting
- **No audio response**: Check microphone is connected and not muted
- **Too sensitive**: Reduce multipliers in update()
- **Too jittery**: Increase smoothing value
- **Delayed response**: Decrease smoothing value

## Next
06-video-input: Using video and webcam as texture sources
