# Sampler Instrument: Simpler-Style

Add a polyphonic sampler instrument that loads a single sample and plays it chromatically across the keyboard, similar to Ableton's Simpler.

---

## Overview

**What it does:**
- Load a single audio sample (WAV only for now)
- Play it polyphonically with pitch shifting (noteOn/noteOff)
- ADSR envelope per voice
- Root note setting (what pitch the sample is at 1.0x speed)
- Loop points (optional)
- Basic controls: volume, pan, attack, decay, sustain, release

**Relation to existing operators:**
- `SamplePlayer`: Triggers samples by index, no pitch control per-note, no ADSR
- `PolySynth`: Voice management + ADSR, but generates waveforms not samples
- `Sampler` (new): Combines sample playback with synth-style voice management

---

## Implementation

### Files to Create

| File | Purpose |
|------|---------|
| `addons/vivid-audio/include/vivid/audio/sampler.h` | Header with Sampler class |
| `addons/vivid-audio/src/sampler.cpp` | Implementation |

### Files to Modify

| File | Change |
|------|--------|
| `addons/vivid-audio/include/vivid/audio/audio.h` | Add `#include "sampler.h"` |

---

## API Design

```cpp
class Sampler : public AudioOperator {
public:
    // Parameters (Param<T> for UI control)
    Param<float> volume{"volume", 0.8f, 0.0f, 2.0f};
    Param<float> attack{"attack", 0.01f, 0.0f, 5.0f};
    Param<float> decay{"decay", 0.1f, 0.0f, 5.0f};
    Param<float> sustain{"sustain", 1.0f, 0.0f, 1.0f};
    Param<float> release{"release", 0.3f, 0.0f, 10.0f};

    Param<int> rootNote{"rootNote", 60, 0, 127};  // MIDI note of original pitch
    Param<int> maxVoices{"maxVoices", 8, 1, 32};

    // Sample loading
    void loadSample(const std::string& path);

    // Loop control
    void setLoop(bool enabled);
    void setLoopPoints(float startSec, float endSec);

    // Playback (MIDI-style)
    void noteOn(int midiNote, float velocity = 1.0f);
    void noteOff(int midiNote);
    void allNotesOff();

    // Voice stealing mode
    void setVoiceStealMode(VoiceStealMode mode);  // Oldest, Quietest, None
};
```

---

## Voice Structure

```cpp
struct Voice {
    int midiNote = -1;
    double position = 0.0;           // Fractional sample position
    float pitch = 1.0f;              // Calculated from midiNote vs rootNote
    float velocity = 1.0f;

    // ADSR envelope (copied from PolySynth pattern)
    EnvelopeStage envStage = EnvelopeStage::Idle;
    float envValue = 0.0f;
    float envProgress = 0.0f;
    float releaseStartValue = 0.0f;

    uint64_t noteId = 0;             // For voice stealing

    bool isActive() const { return envStage != EnvelopeStage::Idle; }
};
```

---

## Key Implementation Details

### Pitch Calculation
```cpp
// MIDI note to pitch multiplier relative to root note
float pitchFromNote(int midiNote) const {
    int semitones = midiNote - static_cast<int>(rootNote);
    return std::pow(2.0f, semitones / 12.0f);
}
```

### Sample Interpolation
Use linear interpolation (same as SamplePlayer):
```cpp
double pos = voice.position;
size_t idx = static_cast<size_t>(pos);
float frac = static_cast<float>(pos - idx);
float sample = m_samples[idx] * (1.0f - frac) + m_samples[idx + 1] * frac;
```

### Voice Management
Follow PolySynth pattern:
1. `noteOn`: Find free voice or steal oldest/quietest
2. Set pitch from MIDI note, start Attack stage
3. `noteOff`: Find voice by midiNote, start Release stage
4. Voice becomes Idle when envelope reaches 0 after Release

### Loop Handling
```cpp
if (m_loopEnabled && voice.position >= m_loopEnd) {
    voice.position = m_loopStart + (voice.position - m_loopEnd);
}
```

---

## Example Usage

```cpp
void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& sampler = chain.add<Sampler>("piano");
    sampler.loadSample("assets/piano_c4.wav");
    sampler.rootNote = 60;  // Sample is C4
    sampler.attack = 0.01f;
    sampler.decay = 0.5f;
    sampler.sustain = 0.6f;
    sampler.release = 1.0f;

    chain.audioOutput("piano");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& midi = chain.get<MidiIn>("midi");
    auto& sampler = chain.get<Sampler>("piano");

    for (const auto& e : midi.events()) {
        if (e.type == MidiEventType::NoteOn) {
            sampler.noteOn(e.note, e.velocity / 127.0f);
        } else if (e.type == MidiEventType::NoteOff) {
            sampler.noteOff(e.note);
        }
    }

    chain.process(ctx);
}
```

---

## Implementation Steps

1. **Create header** (`sampler.h`)
   - Voice struct with ADSR state
   - Sampler class with parameters
   - Public API methods

2. **Implement core** (`sampler.cpp`)
   - `loadSample()`: Use AssetLoader::resolve() for path, then loadWAV()
   - `noteOn()`/`noteOff()`: Voice allocation + pitch calculation
   - `generateBlock()`: Per-voice sample playback with interpolation + envelope

3. **Add to audio.h** include

4. **Test with example** - Simple MIDI keyboard → Sampler chain

---

## Estimated Effort

~1-2 days: Combines proven patterns from SamplePlayer (sample loading, interpolation) and PolySynth (voice management, ADSR).
