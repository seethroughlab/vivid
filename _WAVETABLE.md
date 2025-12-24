# Wavetable Synth Enhancement Roadmap

Feature ideas to expand WavetableSynth sound variety, with architectural decisions on what belongs inside the synth vs as separate chainable operators.

**Key File:** `addons/vivid-audio/include/vivid/audio/wavetable_synth.h`

---

## Architectural Decision

Vivid's philosophy is composable operators, not monolithic all-in-one plugins like Serum.

### Inside WavetableSynth (per-voice, oscillator-level)
Features that need per-voice state or are intrinsic to the oscillator:
- **Unison** - spawns multiple voices per note
- **Sub oscillator** - tracks same note/envelope as main
- **Portamento** - slides between notes on same voice
- **Velocity sensitivity** - per-voice, captured at noteOn
- **Pitch bend** - affects frequency calculation directly
- **Warp modes** - operates on oscillator phase before lookup
- **Per-voice filter + envelope** - each voice needs own filter state

### Outside as Separate Operators (chainable)
Post-synthesis processing or already exists:
- **Noise** - already have noise generators, just mix in chain
- **Global filter** - `LadderFilter`, `AudioFilter` already exist
- **LFO modulation** - `LFO` operator exists, connect in update()
- **Effects** - Chorus, delay, reverb already separate
- **Arpeggiator** - could be a `NoteProcessor` that feeds any synth

---

## Current Capabilities

- 8-voice polyphony with voice stealing
- 6 built-in wavetables (Basic, Analog, Digital, Vocal, Texture, PWM)
- Wavetable morphing via `position` (0-1)
- Stereo detune (cents)
- ADSR envelope (amplitude only)
- `generateFromHarmonics()`, `generateFromFormula()`
- File loading stubbed but not implemented

---

## Features to Add to WavetableSynth

### Phase 1: Quick Wins

**1. Unison Voices with Detune Spread**

**Impact:** Massive, wide sounds - essential for modern synth tones

```cpp
Param<int> unisonVoices{"unisonVoices", 1, 1, 8};
Param<float> unisonSpread{"unisonSpread", 20.0f, 0.0f, 100.0f}; // cents
Param<float> unisonStereo{"unisonStereo", 1.0f, 0.0f, 1.0f};    // L/R spread
```

**Implementation:**
1. In `noteOn()`, spawn `unisonVoices` copies of the note
2. Detune each voice: `freq * pow(2, (spreadCents * voiceOffset) / 1200.0)`
3. Pan voices across stereo field based on `unisonStereo`
4. Divide amplitude by `sqrt(unisonVoices)` to maintain volume
5. Track unison group so `noteOff()` releases all copies

---

**2. Sub Oscillator**

**Impact:** Beefier bass, fuller low end

```cpp
Param<float> subLevel{"subLevel", 0.0f, 0.0f, 1.0f};
Param<int> subOctave{"subOctave", -1, -2, -1};  // -1 or -2 octaves
enum class SubWave { Sine, Square };
SubWave subWave = SubWave::Sine;
```

**Implementation:**
1. Add sub oscillator phase tracking per voice
2. In `process()`, generate sub waveform at `freq / (2^octave)`
3. Mix with main oscillator: `output = main + sub * subLevel`

---

**3. Portamento/Glide**

**Impact:** Expressive leads, acid basslines

```cpp
Param<float> portamento{"portamento", 0.0f, 0.0f, 2000.0f}; // ms
enum class PortaMode { Always, Legato };
PortaMode portaMode = PortaMode::Always;
```

**Implementation:**
1. Track `targetFreq` and `currentFreq` per voice
2. On `noteOn()`: set `targetFreq`, keep `currentFreq` if legato
3. In `process()`: interpolate `currentFreq` toward `targetFreq`
4. Use exponential curve: `currentFreq += (targetFreq - currentFreq) * rate`

---

**4. Velocity Sensitivity**

```cpp
Param<float> velToVolume{"velToVolume", 1.0f, 0.0f, 1.0f};
Param<float> velToFilter{"velToFilter", 0.0f, 0.0f, 1.0f};
Param<float> velToAttack{"velToAttack", 0.0f, -1.0f, 1.0f};
```

**Implementation:**
1. Store velocity per voice in `Voice` struct
2. Scale amplitude: `envValue * lerp(1.0, velocity, velToVolume)`
3. Modify attack time: `attack * (1 - velToAttack * (1 - velocity))`

---

### Phase 2: Warp Modes

**5. Wavetable Warp**

**Impact:** Dramatically expand timbral range from same wavetable

```cpp
enum class WarpMode {
    None, Sync, BendPlus, BendMinus, Mirror,
    Asym, Quantize, FM, Flip
};
WarpMode warpMode = WarpMode::None;
Param<float> warpAmount{"warpAmount", 0.0f, 0.0f, 1.0f};
```

**Implementation per mode:**

1. **Sync** - Reset phase at `warpAmount` frequency ratio
   ```cpp
   if (phase >= 1.0 / syncRatio) phase = fmod(phase, 1.0 / syncRatio);
   ```

2. **Bend+/Bend-** - Phase distortion
   ```cpp
   float warpedPhase = pow(phase, 1.0 + warpAmount); // or 1 / (1 + warpAmount)
   ```

3. **Mirror** - Reflect at midpoint
   ```cpp
   if (phase > 0.5) phase = 1.0 - phase;
   ```

4. **FM (self)** - Self-modulation
   ```cpp
   phase += sin(phase * 2 * PI) * warpAmount;
   ```

5. **Quantize** - Reduce phase resolution
   ```cpp
   int steps = 4 + (1 - warpAmount) * 252;
   phase = floor(phase * steps) / steps;
   ```

---

### Phase 3: Per-Voice Filter

**6. Integrated Filter + Filter Envelope**

**Impact:** Self-contained subtractive synthesis with polyphonic filter sweeps

```cpp
// Filter
enum class FilterType { LP12, LP24, HP12, BP, Notch };
FilterType filterType = FilterType::LP24;
Param<float> filterCutoff{"filterCutoff", 20000.0f, 20.0f, 20000.0f};
Param<float> filterResonance{"filterResonance", 0.0f, 0.0f, 1.0f};
Param<float> filterKeytrack{"filterKeytrack", 0.0f, 0.0f, 1.0f};

// Filter Envelope
Param<float> filterAttack{"filterAttack", 0.01f, 0.001f, 10.0f};
Param<float> filterDecay{"filterDecay", 0.3f, 0.001f, 10.0f};
Param<float> filterSustain{"filterSustain", 0.0f, 0.0f, 1.0f};
Param<float> filterRelease{"filterRelease", 0.3f, 0.001f, 10.0f};
Param<float> filterEnvAmount{"filterEnvAmount", 0.0f, -1.0f, 1.0f};
```

**Implementation:**
1. Add per-voice filter state (biquad coefficients + history)
2. Add per-voice filter envelope tracking
3. Compute modulated cutoff: `cutoff + envAmount * filterEnv * (20000 - cutoff)`
4. Apply keytracking: `cutoff * pow(2, (note - 60) / 12.0 * keytrack)`
5. Filter each voice's output before mixing

**Reuse:** Existing `LadderFilter` or `AudioFilter` code

---

## New Separate Operators

### Arpeggiator (new operator)

```cpp
class Arpeggiator : public AudioOperator {
    bool enabled = false;
    enum class Pattern { Up, Down, UpDown, Random, AsPlayed };
    Pattern pattern = Pattern::Up;
    Param<float> rate{"arpRate", 8.0f, 0.25f, 32.0f}; // notes per beat
    Param<int> octaves{"arpOctaves", 1, 1, 4};
    Param<float> gate{"arpGate", 0.5f, 0.1f, 1.0f};
    Param<float> swing{"arpSwing", 0.0f, -0.5f, 0.5f};
    // Outputs note events to connected synth
};
```

### More Built-in Wavetables

Add to `BuiltinTable` enum:
- `Supersaw` - detuned saws (8 frames, increasing detune)
- `Metallic` - FM ratios for bell/metal (8 frames)
- `Organ` - drawbar combinations (8 frames)
- `Bell` - inharmonic partials (8 frames)
- `Sweep` - filter sweep simulation (8 frames)

---

## Files to Modify

- `addons/vivid-audio/include/vivid/audio/wavetable_synth.h` - Add parameters, Voice struct fields
- `addons/vivid-audio/src/wavetable_synth.cpp` - Implement unison, sub, porta, warp, filter
- `addons/vivid-audio/include/vivid/audio/arpeggiator.h` - New file (if building arp)

---

## Implementation Order

**Phase 1 - Quick Wins (biggest variety boost):**
1. Unison voices with spread
2. Sub oscillator
3. Portamento
4. Velocity sensitivity

**Phase 2 - Timbral Expansion:**
5. Warp modes (Sync, Bend, FM, Mirror, Quantize)

**Phase 3 - Classic Subtractive:**
6. Per-voice filter + filter envelope
