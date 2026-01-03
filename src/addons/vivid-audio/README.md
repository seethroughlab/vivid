# vivid-audio

Audio synthesis, sequencing, and analysis for creative coding.

## Installation

This addon is included with Vivid by default. No additional installation required.

## Operators

### Timing & Sequencing
| Operator | Description |
|----------|-------------|
| `Clock` | Master tempo clock with BPM control |
| `Sequencer` | Step sequencer with pattern playback |
| `Euclidean` | Euclidean rhythm generator |

### Drum Synthesis
| Operator | Description |
|----------|-------------|
| `Kick` | Synthesized kick drum |
| `Snare` | Synthesized snare drum |
| `HiHat` | Synthesized hi-hat |
| `Clap` | Synthesized clap |

### Synthesis
| Operator | Description |
|----------|-------------|
| `Oscillator` | Basic waveform oscillator |
| `Synth` | Polyphonic synthesizer |
| `Envelope` | ADSR envelope generator |
| `Formant` | Formant synthesis for vocal sounds |

### Effects
| Operator | Description |
|----------|-------------|
| `Delay` | Delay effect |
| `Echo` | Echo with feedback |
| `Reverb` | Reverb effect |
| `Compressor` | Dynamic range compression |
| `Limiter` | Peak limiter |
| `Overdrive` | Distortion/overdrive |
| `Bitcrush` | Bit reduction effect |

### Analysis
| Operator | Description |
|----------|-------------|
| `FFT` | Fast Fourier Transform spectrum |
| `Levels` | Audio level metering |
| `BandSplit` | Multi-band frequency analysis |
| `BeatDetect` | Beat/onset detection |

### Input/Output
| Operator | Description |
|----------|-------------|
| `AudioIn` | Microphone/line input |
| `AudioFile` | Audio file playback |
| `AudioMixer` | Multi-channel mixer |
| `AudioGain` | Volume control |
| `AudioOutput` | System audio output |
| `SampleBank` | Load sample folders |
| `SamplePlayer` | Trigger samples |

## Examples

| Example | Description |
|---------|-------------|
| [audio-reactive](examples/audio-reactive) | Audio analysis driving visuals |
| [drum-machine](examples/drum-machine) | Synthesized drums with sequencer |
| [sample-trigger](examples/sample-trigger) | Sample triggering |
| [formant-pad](examples/formant-pad) | Formant synthesis |

## Quick Start

```cpp
#include <vivid/vivid.h>
#include <vivid/audio/audio.h>

using namespace vivid::audio;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Master clock at 120 BPM
    chain.add<Clock>("clock")
        .bpm(120.0f);

    // 4-step kick pattern
    chain.add<Sequencer>("kick_seq")
        .clock("clock")
        .pattern({1, 0, 0, 0, 1, 0, 0, 0});

    // Synthesized kick
    chain.add<Kick>("kick")
        .trigger("kick_seq");

    // Output to speakers
    chain.add<AudioMixer>("mixer")
        .add("kick");

    chain.add<AudioOutput>("out")
        .input("mixer");
}

void update(Context& ctx) {
    ctx.chain().process();
}

VIVID_CHAIN(setup, update)
```

## Audio-Reactive Visuals

```cpp
// Analyze audio file
chain.add<AudioFile>("audio")
    .file("assets/audio/music.wav");

chain.add<FFT>("fft")
    .input("audio");

chain.add<BeatDetect>("beats")
    .input("audio");

// Use in visuals
chain.add<Noise>("noise")
    .scale([&]() { return fft.bass() * 4.0f; });
```

## API Reference

See [LLM-REFERENCE.md](../../docs/LLM-REFERENCE.md) for complete operator documentation.

## Dependencies

- vivid-core
- miniaudio (bundled)

## License

MIT
