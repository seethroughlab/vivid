# Glitch Effects Roadmap

Inspired by analyzing the Lucky 16 MaxForLive pack by Ned Rush. These effects focus on real-time audio manipulation for performance and sound design.

---

## Core Infrastructure Needed

### 1. Audio Buffer Class

A circular buffer for capturing and playing back audio:

```cpp
class AudioBuffer {
    std::vector<float> m_buffer;  // interleaved stereo
    size_t m_writePos = 0;
    size_t m_capacity;            // in frames
    float m_sampleRate;

public:
    void write(float L, float R);                    // continuous recording
    void read(float phase, float& L, float& R);     // interpolated playback
    void readReverse(float phase, float& L, float& R);
    float lengthInBeats(float bpm) const;
};
```

**Key features:**
- Circular write (always recording last N seconds)
- Interpolated read at arbitrary positions
- Reverse read support
- Configurable size (default: 24 seconds like Lucky 16)

### 2. Tempo Sync System

Synced rate divisions for rhythmic effects:

```cpp
enum class RateDiv {
    Whole,      // 1n   = 1 bar
    Half,       // 2n   = 1/2 bar
    Quarter,    // 4n   = 1 beat
    Eighth,     // 8n   = 1/2 beat
    Sixteenth,  // 16n  = 1/4 beat
    ThirtySecond,
    DottedQuarter,
    DottedEighth,
    TripletQuarter,
    TripletEighth
};

float rateDivToHz(RateDiv div, float bpm);
float rateDivToSamples(RateDiv div, float bpm, float sampleRate);
```

### 3. Trigger/Clock System

For tempo-synced effect triggering:

```cpp
class TriggerClock {
    float m_phase = 0;
    float m_sampleRate;

public:
    bool tick(float bpm, RateDiv div);  // returns true on trigger
    float phase() const;                 // 0-1 progress through cycle
    void reset();
};
```

---

## Buffer-Based Effects

### 1. BeatRepeat

Captures audio and loops a slice rhythmically.

```cpp
class BeatRepeat : public AudioEffect {
    AudioBuffer m_buffer{24.0f};  // 24 seconds

    // Trigger controls
    Param<float> bpm{"bpm", 120.0f, 20.0f, 300.0f};
    RateDiv triggerRate = RateDiv::Eighth;
    Param<float> chance{"chance", 0.5f, 0.0f, 1.0f};  // probability

    // Repeat controls
    RateDiv repeatSize = RateDiv::Sixteenth;  // slice length
    Param<int> repeatCount{"repeatCount", 4, 1, 16};
    Param<float> decay{"decay", 0.0f, 0.0f, 1.0f};   // volume decay per repeat

    // Randomization
    Param<float> randomSlice{"randomSlice", 0.0f, 0.0f, 1.0f};  // random slice position
    Param<float> randomPitch{"randomPitch", 0.0f, 0.0f, 1.0f};  // random pitch per repeat
};
```

**Algorithm:**
1. Continuously write input to circular buffer
2. On trigger (based on `triggerRate` and `chance`):
   - Capture current write position as slice start
   - Calculate slice length from `repeatSize`
   - Switch to playback mode
3. During playback:
   - Loop the captured slice `repeatCount` times
   - Apply `decay` envelope
   - Optionally randomize playback position/pitch
4. After repeats complete, return to passthrough

---

### 2. Reverse

Real-time reverse playback of captured audio.

```cpp
class Reverse : public AudioEffect {
    AudioBuffer m_buffer{24.0f};

    Param<float> bpm{"bpm", 120.0f, 20.0f, 300.0f};
    RateDiv triggerRate = RateDiv::Quarter;
    Param<float> chance{"chance", 0.5f, 0.0f, 1.0f};

    RateDiv reverseLength = RateDiv::Quarter;  // how much to reverse
    Param<float> mix{"mix", 1.0f, 0.0f, 1.0f};
};
```

**Algorithm:**
1. Continuously write input to buffer
2. On trigger:
   - Mark reverse start position
   - Calculate reverse length in samples
3. During reverse:
   - Read buffer backwards from start position
   - Crossfade at boundaries to avoid clicks
4. After reverse completes, return to passthrough

---

### 3. Stutter / Roll

Like BeatRepeat but with volume envelope (builds or decays).

```cpp
class Stutter : public AudioEffect {
    AudioBuffer m_buffer{24.0f};

    Param<float> bpm{"bpm", 120.0f, 20.0f, 300.0f};
    RateDiv triggerRate = RateDiv::Quarter;
    Param<float> chance{"chance", 0.5f, 0.0f, 1.0f};

    RateDiv stutterSize = RateDiv::Sixteenth;
    Param<int> stutterCount{"stutterCount", 8, 1, 32};

    enum class EnvelopeType { Flat, Decay, Build, Triangle };
    EnvelopeType envelope = EnvelopeType::Decay;
    Param<float> envelopeAmount{"envelopeAmount", 0.5f, 0.0f, 1.0f};
};
```

**Envelope types:**
- **Flat**: No volume change
- **Decay**: Gets quieter (classic stutter)
- **Build**: Gets louder (reverse buildup)
- **Triangle**: Quiet → loud → quiet

---

### 4. Scratch

DJ-style scratch effect with varispeed playback.

```cpp
class Scratch : public AudioEffect {
    AudioBuffer m_buffer{24.0f};

    Param<float> bpm{"bpm", 120.0f, 20.0f, 300.0f};
    RateDiv triggerRate = RateDiv::Quarter;
    Param<float> chance{"chance", 0.5f, 0.0f, 1.0f};

    // Scratch motion
    Param<float> speed{"speed", 1.0f, 0.125f, 4.0f};    // playback speed
    Param<float> speedRandom{"speedRandom", 0.5f, 0.0f, 1.0f};

    enum class Motion { Forward, Backward, BackForth, Random };
    Motion motion = Motion::BackForth;

    Param<float> scratchLength{"scratchLength", 0.25f, 0.0f, 1.0f};  // in beats
};
```

**Algorithm:**
1. On trigger, capture slice
2. Modulate read position with:
   - Triangle wave for back-forth motion
   - Variable speed (optionally randomized)
   - Quantized speed changes (0.125x, 0.25x, 0.5x, 1x, 2x)
3. Crossfade at direction changes

---

### 5. Stretch

Time-stretch without pitch change (granular-style).

```cpp
class Stretch : public AudioEffect {
    AudioBuffer m_buffer{24.0f};

    Param<float> bpm{"bpm", 120.0f, 20.0f, 300.0f};
    RateDiv triggerRate = RateDiv::Half;
    Param<float> chance{"chance", 0.5f, 0.0f, 1.0f};

    Param<float> stretchFactor{"stretchFactor", 2.0f, 0.25f, 4.0f};
    Param<float> grainSize{"grainSize", 50.0f, 10.0f, 200.0f};  // ms
    Param<float> grainRandom{"grainRandom", 0.1f, 0.0f, 0.5f};
};
```

**Algorithm:**
- Use overlapping grains to stretch time
- Maintain pitch by keeping grain playback rate at 1x
- Overlap-add with window function (Hann)

---

## Non-Buffer Effects

### 6. FrequencyShift (Bode Shifter)

Shifts all frequencies by a fixed Hz amount (not pitch scaling).

```cpp
class FrequencyShift : public AudioEffect {
    // Hilbert transform state
    float m_hilbertI[HILBERT_TAPS];
    float m_hilbertQ[HILBERT_TAPS];
    float m_oscPhase = 0;

    Param<float> shift{"shift", 0.0f, -1000.0f, 1000.0f};  // Hz
    Param<float> mix{"mix", 1.0f, 0.0f, 1.0f};

    // For tempo-synced modulation
    Param<float> bpm{"bpm", 120.0f, 20.0f, 300.0f};
    Param<float> modDepth{"modDepth", 0.0f, 0.0f, 500.0f};  // Hz
    RateDiv modRate = RateDiv::Quarter;
};
```

**Algorithm:**
1. Apply Hilbert transform to get I/Q (analytic signal)
2. Multiply by complex oscillator at shift frequency
3. Take real part of result

```cpp
// Simplified frequency shift
float I = hilbertI(input);  // in-phase (original delayed)
float Q = hilbertQ(input);  // quadrature (90° shifted)

float oscI = cos(m_oscPhase);
float oscQ = sin(m_oscPhase);

float output = I * oscI - Q * oscQ;  // up-shift
// or: I * oscI + Q * oscQ;          // down-shift

m_oscPhase += 2 * PI * shift / sampleRate;
```

**Character:** Inharmonic, metallic, robotic - different from pitch shifting.

---

### 7. Tape Stop

Simulates a tape deck slowing down or speeding up.

```cpp
class TapeStop : public AudioEffect {
    AudioBuffer m_buffer{2.0f};  // short buffer
    float m_playbackRate = 1.0f;

    Param<float> bpm{"bpm", 120.0f, 20.0f, 300.0f};
    RateDiv triggerRate = RateDiv::Whole;
    Param<float> chance{"chance", 0.5f, 0.0f, 1.0f};

    Param<float> stopTime{"stopTime", 500.0f, 50.0f, 2000.0f};  // ms to stop
    Param<float> startTime{"startTime", 200.0f, 50.0f, 1000.0f}; // ms to restart

    enum class Mode { Stop, Start, StopStart };
    Mode mode = Mode::StopStart;
};
```

**Algorithm:**
1. On trigger, begin decelerating playback rate
2. Rate follows exponential curve: `rate *= decayFactor`
3. Pitch drops naturally as rate decreases
4. Optionally reverse (tape start = speed up)

---

### 8. Glitch (Multi-Effect)

Combines multiple glitch behaviors with probability.

```cpp
class Glitch : public AudioEffect {
    // Sub-effects
    BeatRepeat m_repeat;
    Reverse m_reverse;
    Stutter m_stutter;
    TapeStop m_tapeStop;

    Param<float> bpm{"bpm", 120.0f, 20.0f, 300.0f};
    RateDiv triggerRate = RateDiv::Eighth;

    // Probability per effect type
    Param<float> repeatChance{"repeatChance", 0.3f, 0.0f, 1.0f};
    Param<float> reverseChance{"reverseChance", 0.2f, 0.0f, 1.0f};
    Param<float> stutterChance{"stutterChance", 0.3f, 0.0f, 1.0f};
    Param<float> tapeChance{"tapeChance", 0.1f, 0.0f, 1.0f};

    Param<float> mix{"mix", 0.5f, 0.0f, 1.0f};
};
```

---

## Implementation Order

### Phase 1 - Core Infrastructure
1. `AudioBuffer` class with circular write + interpolated read
2. `TriggerClock` with tempo sync
3. Rate division utilities

### Phase 2 - Essential Effects
4. **BeatRepeat** - most versatile, foundation for others
5. **Reverse** - simple but impactful
6. **Stutter** - variation on repeat with envelopes

### Phase 3 - Advanced Effects
7. **Scratch** - varispeed playback
8. **TapeStop** - classic DJ effect
9. **FrequencyShift** - requires Hilbert transform

### Phase 4 - Meta Effect
10. **Glitch** - combines all with probability

---

## Files to Create

```
addons/vivid-audio/include/vivid/audio/
  glitch/
    audio_buffer.h      // circular buffer
    trigger_clock.h     // tempo sync
    beat_repeat.h
    reverse.h
    stutter.h
    scratch.h
    tape_stop.h
    frequency_shift.h
    glitch.h            // meta-effect

addons/vivid-audio/src/
  glitch/
    audio_buffer.cpp
    beat_repeat.cpp
    reverse.cpp
    stutter.cpp
    scratch.cpp
    tape_stop.cpp
    frequency_shift.cpp
    glitch.cpp
```

---

## Example Usage

```cpp
void setup(Context& ctx) {
    auto& synth = chain.add<WavetableSynth>("synth");
    synth.loadBuiltin(BuiltinTable::Digital);

    auto& repeat = chain.add<BeatRepeat>("repeat");
    repeat.bpm = 128.0f;
    repeat.triggerRate = RateDiv::Eighth;
    repeat.chance = 0.4f;
    repeat.repeatSize = RateDiv::Sixteenth;
    repeat.repeatCount = 4;
    repeat.decay = 0.3f;

    auto& reverse = chain.add<Reverse>("reverse");
    reverse.bpm = 128.0f;
    reverse.triggerRate = RateDiv::Half;
    reverse.chance = 0.2f;
}
```
