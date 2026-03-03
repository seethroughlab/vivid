# WaveTableSynth Improvements Plan

Improvement areas for the WaveTableSynth operator, prioritised by impact-to-effort ratio and compared against Serum and Pigments as reference targets.

**Source file:** `operators/audio/wavetable_synth/wavetable_synth.cpp` (1014 lines, single-file operator)

---

## Dependency Graph

```
Phase 1: Band-limited playback (mipmap)
    |
    +---> Phase 4: Richer wavetable content (new tables should be band-limited from the start)

Phase 2: Filter drive/saturation        (independent)
Phase 3: Noise oscillator               (independent)
Phase 5: More filter types              (independent)
Phase 6: Sub oscillator waveforms       (independent)
Phase 7: Unison expansion               (independent)
```

Phases 2, 3, 5, 6, 7 have no inter-dependencies and can be implemented in any order or in parallel. Phase 4 depends on Phase 1 so that new wavetable types are band-limited from the start.

---

## Priority Summary

| # | Feature | Impact | Complexity | Runtime Cost |
|---|---------|--------|------------|-------------|
| 1 | Band-limited playback (mipmap) | Highest | Medium (init-time FFT) | Zero (table lookup) |
| 2 | Filter drive/saturation | High | Trivial (~5 lines) | Negligible (1 tanh/sample) |
| 3 | Noise oscillator | High | Low (~30 lines) | Negligible |
| 4 | Richer wavetable content | High | Medium (~200 lines) | Zero (pre-computed) |
| 5 | More filter types | Medium-High | Medium (~150 lines) | Low |
| 6 | Sub oscillator waveforms | Medium | Trivial (~10 lines) | Negligible |
| 7 | Unison expansion | Medium | Low (~40 lines) | Linear with voice count |

---

## Phase 1 — Band-Limited Wavetable Playback (Mipmap Antialiasing)

### Current State
- Wavetable lookup uses bilinear interpolation with no antialiasing
- At high pitches, harmonics above Nyquist fold back as audible aliasing
- This is the single biggest quality gap versus Serum/Pigments

### Desired State
- Mipmap hierarchy of each wavetable pre-computed at init time
- Runtime selects the appropriate mipmap level based on playback frequency
- No aliasing at any pitch, with zero per-sample overhead

### Algorithm

**Init-time mipmap generation (per wavetable):**
1. For each frame, FFT the 2048-sample waveform into frequency domain
2. Create `log2(SAMPLES_PER_FRAME)` = 11 mipmap levels
3. For level `L`, zero all bins above `SAMPLES_PER_FRAME / 2^(L+1)` (i.e. keep only harmonics that stay below Nyquist at the playback rate for that level)
4. IFFT back to time domain and store as a separate frame buffer

**Runtime level selection:**
```
level = floor(log2(playback_freq / fundamental_table_freq))
level = clamp(level, 0, max_level)
```
Then sample from the appropriate mipmap level using the existing bilinear interpolation. Optionally interpolate between adjacent levels for smooth transitions.

### Storage
- Each mipmap level is half the effective bandwidth of the previous, but all stored at 2048 samples (for uniform indexing). Total memory per wavetable: ~11x current, well within budget given MAX_FRAMES=256 and 2048 floats/frame
- Alternatively, store only 5-6 practically-needed levels (covers fundamentals up to ~10 kHz at 48 kHz SR)

### Reference Files
- `operators/control/fft_analysis/fft_analysis.cpp` — in-place radix-2 Cooley-Tukey FFT already in the codebase
- All wavetables are already pre-computed in the constructor; mipmap generation follows the same pattern

### Parameters
No new user-facing parameters. Mipmap selection is automatic.

---

## Phase 2 — Filter Drive / Saturation

### Current State
- 5 biquad filter types (LP12, LP24, HP12, BP, Notch) with no drive stage
- Filters sound clean and clinical

### Desired State
- Adjustable drive/saturation applied to the filter input signal
- Ranges from subtle warmth to aggressive distortion
- Matches the "Drive" knob on Serum's filter section

### Algorithm
Apply soft-clipping before the biquad filter stage:
```cpp
// Before existing filter call:
if (drive > 0.001f) {
    float d = 1.0f + drive * 7.0f;            // drive range [1, 8]
    float norm = 1.0f / std::tanh(d);
    sample = std::tanh(sample * d) * norm;     // gain-compensated soft clip
}
```

The gain compensation (`norm`) keeps perceived loudness stable as drive increases, matching the pattern in `distortion.cpp`.

### Reference Files
- `operators/audio/distortion/distortion.cpp` — `tanh(in * d) * norm` with OnePole tone filter
- `operators/audio/drum_common/drum_dsp.h` — `soft_clip(float x, float drive)` returning `tanh(x * (1 + drive * 3))`

### New Parameters
| Parameter | Type | Default | Range | Group |
|-----------|------|---------|-------|-------|
| `filter_drive` | float | 0.0 | 0.0–1.0 | Filter |

---

## Phase 3 — Noise Oscillator

### Current State
- No noise source in the signal path
- Serum has 200+ noise shapes; Pigments has dedicated noise sources
- Missing breathiness, texture, attack transients

### Desired State
- White and pink noise generators mixed into the voice signal
- Level control and type selection per-voice

### Algorithm
```cpp
// In per-voice, per-sample loop, after wavetable + sub:
if (noise_level > 0.001f) {
    float n = (noise_type == 0) ? white_noise.next() : pink_noise.next();
    sample += n * noise_level * amp_env_value;  // envelope-gated noise
}
```

Each voice gets its own noise generator state to avoid correlation artifacts in unison.

### Reference Files
- `src/operator_api/audio_dsp.h` — `WhiteNoise` struct (LCG PRNG, already in codebase)
- `operators/audio/drum_common/drum_dsp.h` — `PinkNoise` struct (Voss-McCartney, 8 octave bands)

### New Parameters
| Parameter | Type | Default | Range/Choices | Group |
|-----------|------|---------|---------------|-------|
| `noise_level` | float | 0.0 | 0.0–1.0 | Noise |
| `noise_type` | int | 0 | {"White", "Pink"} | Noise |

### Voice Struct Addition
```cpp
WhiteNoise white_noise;
PinkNoise  pink_noise;
```

---

## Phase 4 — Richer Wavetable Content

### Current State
- 6 built-in wavetable types, each with only 5–8 frames
- Too sparse for expressive position sweeps (Serum uses hundreds of frames per table)
- Limited timbral palette (no metallic/bell, no rich harmonic series, limited formant morphing)

### Desired State
- Increase existing tables to 32–64 frames for smoother morphing
- Add 3 new wavetable types expanding the timbral range
- All new content band-limited via Phase 1 mipmaps

### New Wavetable Types

**Formant (64 frames)**
Expanded vowel morphing across 8 vowel targets (A, E, I, O, U, Ae, Oe, nasal). Each vowel defined by 3–5 formant center frequencies with Gaussian bandwidth envelopes. Frames interpolate smoothly between vowels:
```
frame 0–7:   A → E
frame 8–15:  E → I
frame 16–23: I → O
frame 24–31: O → U
frame 32–63: extended vowel combinations and nasal resonances
```
Generation: for each frame, sum sine partials weighted by formant response curves at each harmonic.

**Harmonic (64 frames)**
Pure additive synthesis with controllable partial weighting. Frame 0 = fundamental only, frame 63 = full harmonic series (64 partials). Intermediate frames use different spectral shapes:
```
frame 0–15:  linear partial rolloff (increasing count)
frame 16–31: odd-harmonic emphasis (hollow → full)
frame 32–47: even-harmonic emphasis (warm → bright)
frame 48–63: spectral tilt variations (bass-heavy → treble-heavy)
```
Generation: additive synthesis with `N_harmonics` increasing with frame index, shaped by spectral envelope functions.

**Metallic (32 frames)**
Inharmonic partials for bell, gong, and metallic percussion tones. Uses frequency ratios inspired by actual metal vibration modes:
```
frame 0–7:   simple bell (ratios: 1.0, 2.0, 3.0, 4.2, 5.4)
frame 8–15:  complex bell (ratios: 1.0, 1.5, 2.3, 3.1, 4.7, 6.2)
frame 16–23: gong-like (more inharmonic, lower ratios)
frame 24–31: cymbal-like (dense inharmonic cluster)
```
Generation: sum of sine waves at inharmonic frequency ratios, morphing from simple to complex across frames.

### Reference Files
- `operators/audio/drum_hihat/drum_hihat.cpp` — ring oscillator bank with inharmonic ratios (205.3, 304.4, 369.6, 522.7, 540.0, 800.0 Hz)
- `operators/audio/drum_cymbal/drum_cymbal.cpp` — 12 inharmonic ring oscillator frequencies
- Existing `generate_vocal()` — pattern for formant-based synthesis

### Existing Table Updates
Increase frame counts for existing types:
| Table | Current Frames | Target Frames |
|-------|---------------|--------------|
| Basic | 8 | 32 |
| Analog | 8 | 32 |
| Digital | 8 | 32 |
| Vocal | 5 | 32 |
| Texture | 8 | 32 |
| PWM | 8 | 32 |

### Parameter Changes
Update `wavetable` enum choices from 6 to 9: add `"Formant"`, `"Harmonic"`, `"Metallic"`.

---

## Phase 5 — More Filter Types

### Current State
- 5 biquad-based filter types (LP12, LP24, HP12, BP, Notch)
- Serum offers 75+ filter types; Pigments has 11 hardware-modelled types
- Missing: resonant delay-based filters, self-oscillating ladder, vowel resonances

### Desired State
Add 3 filter types covering distinct sonic territory not served by the existing biquad set.

### New Filter Types

**Comb Filter**
Delay line with feedback producing metallic/resonant comb teeth. Karplus-Strong character at short delay times.
```cpp
struct CombFilter {
    static constexpr int MAX_DELAY = 2048;
    float buffer[MAX_DELAY] = {};
    int   write_pos = 0;

    float process(float input, float delay_samples, float feedback) {
        int read_pos = write_pos - static_cast<int>(delay_samples);
        if (read_pos < 0) read_pos += MAX_DELAY;
        float delayed = buffer[read_pos];
        float out = input + delayed * feedback;
        buffer[write_pos] = out;
        write_pos = (write_pos + 1) % MAX_DELAY;
        return out;
    }
};
```
Map `filter_cutoff` to delay time: `delay_samples = sample_rate / cutoff_hz`. Map `filter_resonance` to feedback: `feedback = resonance * 0.98` (cap below 1.0 for stability).

**Ladder (Moog-style 4-pole)**
Cascaded one-pole filters with non-linear feedback for self-oscillation character.
```cpp
struct LadderFilter {
    float stage[4] = {};
    float delay[4] = {};

    float process(float input, float cutoff_norm, float resonance) {
        // cutoff_norm = 2 * pi * cutoff / sample_rate, clamped to [0, 1]
        float fb = resonance * 4.0f;  // 4x feedback for self-oscillation at res=1
        float in = input - fb * stage[3];
        in = std::tanh(in);           // non-linear input saturation
        for (int i = 0; i < 4; i++) {
            float s = (in + delay[i]) * 0.5f;  // trapezoidal integration
            delay[i] = in;
            stage[i] = stage[i] + cutoff_norm * (s - stage[i]);
            in = stage[i];
        }
        return stage[3];
    }
};
```
Natural self-oscillation at high resonance. The `tanh` on the input gives the characteristic Moog warmth.

**Formant Filter**
Parallel bandpass filters tuned to vowel formant frequencies, blended by the cutoff parameter.
```cpp
struct FormantFilter {
    // 5 vowel presets: A, E, I, O, U
    // Each defined by 3 formant frequencies + bandwidths
    static constexpr float formants[5][3] = {
        {800, 1150, 2900},  // A
        {350, 2000, 2800},  // E
        {270, 2300, 3000},  // I
        {450,  800, 2830},  // O
        {325,  700, 2530},  // U
    };
    SVF bands[3];  // 3 parallel bandpass filters

    float process(float input, float morph, float resonance, float sr) {
        // morph [0,1] sweeps A→E→I→O→U
        // interpolate formant frequencies between adjacent vowels
        // sum 3 bandpass outputs
    }
};
```
Map `filter_cutoff` to formant morph position (20 Hz → A, 20 kHz → U, log-scaled). Map `filter_resonance` to bandpass Q.

### Reference Files
- `operators/audio/drum_common/drum_dsp.h` — SVF struct (Chamberlin state-variable filter) with LP/HP/BP modes
- Existing biquad code in wavetable_synth.cpp for coefficient calculation patterns

### Parameter Changes
Extend `filter_type` enum from 5 to 8 choices: add `"Comb"`, `"Ladder"`, `"Formant"`.

---

## Phase 6 — Sub Oscillator Waveforms

### Current State
- Sub oscillator is sine-only: `sin(phase * TWO_PI_F)`
- Selectable -1 or -2 octave
- Serum offers 7 sub waveforms

### Desired State
- 5 sub waveform choices: Sine, Triangle, Saw, Square, Noise
- Same octave selection (-1, -2)
- Different waveforms give different bass character (triangle = soft, square = punchy, saw = full, noise = rumble)

### Algorithm
Replace the hardcoded sine with a waveform selector using the existing `audio_dsp::waveform()`:
```cpp
float sub_sample;
if (sub_waveform < 4) {
    // 0=sine, 1=triangle, 2=saw, 3=square
    sub_sample = static_cast<float>(audio_dsp::waveform(voice.sub_phase, sub_waveform));
} else {
    // 4=noise (sub-bass rumble)
    sub_sample = voice.white_noise.next();
}
```

### Reference Files
- `src/operator_api/audio_dsp.h` — `waveform(double phase, int type)` with types 0=sine, 1=saw, 2=square, 3=triangle

Note: the `audio_dsp::waveform` enum order is sine=0, saw=1, square=2, triangle=3. The sub parameter enum should present them in musical order (Sine, Triangle, Saw, Square, Noise) and map indices accordingly.

### New Parameters
| Parameter | Type | Default | Range/Choices | Group |
|-----------|------|---------|---------------|-------|
| `sub_waveform` | int | 0 | {"Sine", "Triangle", "Saw", "Square", "Noise"} | Sub |

---

## Phase 7 — Unison Expansion

### Current State
- Maximum 8 unison voices per note
- Linear (even) detuning distribution only
- `unison_spread` in cents, `unison_stereo` for panning

### Desired State
- Maximum 16 unison voices per note
- 3 spread distribution modes for different unison characters
- Serum supports up to 16 voices with multiple spread modes

### Spread Distribution Modes

**Linear (current behavior)**
Voices evenly spaced across the detune range:
```
offset[i] = (i / (N-1) - 0.5) * spread_cents
```

**Exponential**
Wider spacing at extremes, tighter in the center. Gives a "wider" perceived stereo image with less center-frequency smearing:
```
t = i / (N-1) - 0.5                         // [-0.5, 0.5]
offset[i] = sign(t) * pow(abs(t) * 2, 1.5) * 0.5 * spread_cents
```

**Random**
Per-voice random detune offsets, recomputed on each note trigger. Creates Serum-style "analog drift" character:
```
offset[i] = (random_float() - 0.5) * spread_cents  // new seed per note-on
```
Store offsets in the Voice struct so they're stable for the note's duration.

### Voice Budget
With `kMaxVoices = 16` (already the polyphony limit), 16 unison voices on a single note will consume the entire voice pool. This is acceptable — it matches Serum's behavior where high unison counts reduce available polyphony. No structural changes to the voice allocator needed.

### New/Changed Parameters
| Parameter | Type | Default | Range/Choices | Group |
|-----------|------|---------|---------------|-------|
| `unison_voices` | int | 1 | 1–16 (was 1–8) | Unison |
| `unison_spread_mode` | int | 0 | {"Linear", "Exponential", "Random"} | Unison |

### Voice Struct Addition
```cpp
float random_detune_offsets[16];  // populated on note-on for Random mode
```
