# Add Engaging Visuals for Audio Operators

## Goal
Add specialized visualizations for synths and audio filters in the chain visualizer, beyond the current generic waveform display.

## Current State
- **Texture operators**: Show actual GPU texture thumbnails
- **Geometry operators**: Live 3D rotating preview
- **Audio operators**: Basic waveform from AudioBuffer (generic for all)
- **Audio analysis operators**: Specialized (FFT spectrum bars, Levels meters, BeatDetect pulse, BandSplit bands)

## Proposed Visualizations

### Drum Synths

**Kick** - Pitch envelope + amplitude
- Show pitch sweep trajectory (high → low)
- Amplitude envelope overlay
- Color: Deep orange/red

**Snare** - Dual envelope (tone + noise)
- Two-lane visualization: tone (bottom) + noise (top)
- Shows snappy transient attack
- Color: White/gray for noise, warm for tone

**HiHat** - Decay envelope with metallic shimmer
- Fast decay curve
- Small "sparkle" particles representing metallic harmonics
- Color: Bright cyan/silver

**Clap** - Multiple burst visualization
- Show 4 burst envelopes offset in time
- Demonstrates the "hand clap" layering
- Color: Pink/magenta

### Synths

**Synth/PolySynth** - ADSR envelope + waveform icon
- Draw waveform icon (sine/saw/square/triangle) in top half
- Show live ADSR envelope curve in bottom half
- Highlight current envelope stage (A/D/S/R) with color
- Color: Purple

**FMSynth** - 4-operator stack visualization
- Show 4 small oscillator circles arranged per algorithm
- Size/brightness = operator envelope level
- Lines between operators show modulation routing
- Color: Electric blue

**WavetableSynth** - Wavetable position indicator
- Small waveform preview
- Position marker showing current morph position (0-1)
- Color: Teal gradient

### Audio Filters/Effects

**Compressor** - Gain reduction meter
- Vertical bar showing current gain reduction (dB)
- Color gradient: green (0dB) → yellow → red (heavy compression)
- Threshold line indicator

**AudioFilter** - Frequency response curve
- Simple low/high/band pass curve visualization
- Show cutoff position and resonance peak
- Color: Light blue

**Delay/Reverb** - Decay trail visualization
- Horizontal fade-out bars showing delay taps / reverb tail
- Feedback amount affects trail length
- Color: Purple/blue gradient

## Implementation

### File: `core/imgui/chain_visualizer.cpp`

Add after existing audio waveform rendering (~line 1074-1129):

```cpp
// After generic waveform, add specialized visualizations
if (kind == OutputKind::Audio) {
    AudioOperator* audioOp = static_cast<AudioOperator*>(info.op);

    // Drum synths
    if (auto* kick = dynamic_cast<Kick*>(audioOp)) {
        drawKickVisualization(drawList, p, thumbW, thumbH, kick);
    }
    else if (auto* snare = dynamic_cast<Snare*>(audioOp)) {
        drawSnareVisualization(drawList, p, thumbW, thumbH, snare);
    }
    else if (auto* hihat = dynamic_cast<HiHat*>(audioOp)) {
        drawHiHatVisualization(drawList, p, thumbW, thumbH, hihat);
    }
    else if (auto* clap = dynamic_cast<Clap*>(audioOp)) {
        drawClapVisualization(drawList, p, thumbW, thumbH, clap);
    }
    // Synths
    else if (auto* synth = dynamic_cast<Synth*>(audioOp)) {
        drawSynthVisualization(drawList, p, thumbW, thumbH, synth);
    }
    else if (auto* poly = dynamic_cast<PolySynth*>(audioOp)) {
        drawPolySynthVisualization(drawList, p, thumbW, thumbH, poly);
    }
    else if (auto* fm = dynamic_cast<FMSynth*>(audioOp)) {
        drawFMSynthVisualization(drawList, p, thumbW, thumbH, fm);
    }
    // Effects
    else if (auto* comp = dynamic_cast<Compressor*>(audioOp)) {
        drawCompressorVisualization(drawList, p, thumbW, thumbH, comp);
    }
    else {
        // Fall back to generic waveform (existing code)
        drawGenericWaveform(drawList, p, thumbW, thumbH, audioOp);
    }
}
```

### Required Includes
```cpp
#include <vivid/audio/kick.h>
#include <vivid/audio/snare.h>
#include <vivid/audio/hihat.h>
#include <vivid/audio/clap.h>
#include <vivid/audio/synth.h>
#include <vivid/audio/poly_synth.h>
#include <vivid/audio/fm_synth.h>
#include <vivid/audio/compressor.h>
```

### Data Access Needed

Expose these methods if not already public:

| Operator | Method | Returns |
|----------|--------|---------|
| Kick | `envelope()` | float 0-1 |
| Kick | `pitchEnvelope()` | float 0-1 |
| Snare | `toneEnvelope()` | float 0-1 |
| Snare | `noiseEnvelope()` | float 0-1 |
| HiHat | `envelope()` | float 0-1 |
| Clap | `envelope()` | float 0-1 |
| Synth | `envelopeValue()` | float 0-1 |
| Synth | `envelopeStage()` | EnvStage enum |
| Compressor | `getGainReduction()` | float (dB) |

## Priority Order

1. **Drum synths** (Kick, Snare, HiHat, Clap) - most visually distinct
2. **Compressor** - gain reduction meter is standard and useful
3. **Synths** - envelope + waveform icon
4. **FMSynth** - algorithm visualization (more complex)
5. **Filters/effects** - frequency response curves (lower priority)
