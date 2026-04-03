#pragma once

#include <cstdint>

// =============================================================================
// filter_dsp.h — Multi-mode filter DSP: state structures and processing
//
// Used by the core Filter operator (with auto-dup for per-voice processing)
// and available to any package operator that needs filter algorithms.
// =============================================================================

namespace audio_dsp {

enum FilterType {
    FILTER_LP12,       // 0  - 2-pole lowpass (biquad)
    FILTER_LP24,       // 1  - 4-pole lowpass (cascaded biquad)
    FILTER_HP12,       // 2  - 2-pole highpass
    FILTER_BP,         // 3  - bandpass
    FILTER_NOTCH,      // 4  - notch/band-reject
    FILTER_COMB,       // 5  - comb filter (feedback delay)
    FILTER_LADDER,     // 6  - Moog-style ladder (4-pole, nonlinear)
    FILTER_FORMANT,    // 7  - morphing vowel formant
    FILTER_HP24,       // 8  - 4-pole highpass (cascaded biquad)
    FILTER_PEAK,       // 9  - peaking/bell EQ
    FILTER_ALLPASS,    // 10 - phase shift, no amplitude change
    FILTER_BP24,       // 11 - tight bandpass (LP24+HP in series)
    FILTER_DIODE,      // 12 - diode ladder (asymmetric saturation)
    FILTER_MS20,       // 13 - Korg MS-20 style (Sallen-Key, self-oscillating)
    FILTER_COUNT
};

// --- Biquad state (used for LP12/LP24, HP12/HP24, BP, Notch, Peak, Allpass) ---
struct BiquadState {
    float z1[2] = {};  // Two stages for cascaded (LP24/HP24)
    float z2[2] = {};

    void reset();
};

// --- Comb filter ---
struct CombFilterState {
    static constexpr int kMaxDelay = 2048;
    float buffer[kMaxDelay] = {};
    int write_pos = 0;

    void reset();
    float process(float input, float delay_samples, float feedback);
};

// --- Moog ladder filter ---
struct LadderFilterState {
    float stage[4] = {};

    void reset();
    float process(float input, float cutoff_hz, float reso, float sample_rate);
};

// --- Formant/vowel filter ---
struct FormantFilterState {
    float z1[3] = {};
    float z2[3] = {};

    void reset();
    float process(float input, float morph, float reso, float sample_rate);
};

// --- Diode ladder (aggressive saturation variant) ---
struct DiodeLadderState {
    float stage[4] = {};
    float feedback = 0;

    void reset();
    float process(float input, float cutoff_hz, float reso, float sample_rate);
};

// --- Korg MS-20 style ---
struct MS20FilterState {
    float hp = 0, bp = 0, lp = 0;
    float s1 = 0, s2 = 0;

    void reset();
    float process(float input, float cutoff_hz, float reso, float sample_rate);
};

// --- Unified filter state (holds all possible states, only one active at a time) ---
struct FilterState {
    BiquadState      biquad;
    CombFilterState  comb;
    LadderFilterState ladder;
    FormantFilterState formant;
    DiodeLadderState diode;
    MS20FilterState  ms20;

    void reset();

    // Process one sample through the specified filter type.
    // cutoff_hz: filter frequency (20-20000 Hz)
    // reso: resonance (0-1 for biquad/character filters)
    // drive: pre-filter saturation (0-1)
    // ftype: FilterType enum
    // sr: sample rate
    float process(float input, float cutoff_hz, float reso, float drive,
                  int ftype, float sr);
};

} // namespace audio_dsp
