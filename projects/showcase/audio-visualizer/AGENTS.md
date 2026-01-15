# Audio Visualizer

## Vision

Create a stunning audio-reactive visual experience that transforms sound into mesmerizing particle displays. The visualizer should feel alive - breathing, pulsing, and exploding with the music. It should work with both live microphone input and an internal synthesizer for standalone demos.

The aesthetic is modern electronic music visualization: bold colors, particle explosions on beats, smooth flowing motion during quiet passages, and dramatic chromatic effects on transients.

## Key Features

- FFT frequency analysis driving different visual layers
- Beat detection triggering particle bursts and visual impacts
- Three particle layers responding to bass, mids, and highs
- Internal drum synthesizer for standalone demonstration
- Microphone input for live audio reactivity
- Multiple visual presets (Neon, Fire, Ice)
- Central pulsing shape that reacts to overall energy

## Technical Approach

- Audio synthesis: Clock, Sequencer, Kick, Snare, HiHat, AudioMixer
- Audio analysis: FFT, BandSplit, BeatDetect, Levels
- Three Particles layers mapped to frequency bands
- Shape operator for central beat indicator
- Feedback for trails with energy-responsive decay
- Bloom and ChromaticAberration for impact effects

## Operators to Use

| Operator | Purpose |
|----------|---------|
| Clock | Master tempo for internal synth |
| Sequencer | Trigger patterns for drums |
| Kick, Snare, HiHat | Drum synthesis |
| AudioMixer | Mix drum sounds |
| AudioIn | Microphone input |
| FFT | Frequency spectrum analysis |
| BandSplit | Separate bass/mid/high |
| BeatDetect | Detect rhythmic hits |
| Particles (x3) | Bass, mid, high frequency layers |
| Shape | Central beat indicator |
| Feedback | Trail persistence |
| Bloom | Glow effect |
| ChromaticAberration | Impact effect on beats |

## Interaction

- **M**: Toggle between Microphone and internal Synth
- **Space**: Start/Stop internal synth
- **1-3**: Visual presets (Neon, Fire, Ice)
- **Up/Down**: Adjust beat detection sensitivity

## Visual Style

- Dark background with vibrant, saturated particles
- Bass frequencies: Large, slow particles radiating outward
- Mid frequencies: Medium particles with turbulent motion
- High frequencies: Small, fast sparkles with rainbow colors
- Central shape pulses with overall energy and beats
- Chromatic aberration flashes on strong beats
- Feedback trails create motion blur effect
