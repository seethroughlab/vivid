#ifndef VIVID_AUDIO_DSP_H
#define VIVID_AUDIO_DSP_H

#include <cmath>
#include <cstdint>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio_dsp {

// ---------------------------------------------------------------------------
// White noise: LCG-based PRNG -> float [-1,1] and [0,1]
// ---------------------------------------------------------------------------
struct WhiteNoise {
    uint32_t state = 12345;

    float next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<int32_t>(state)) / 2147483648.0f;
    }

    float next_unipolar() {
        return next() * 0.5f + 0.5f;
    }
};

// ---------------------------------------------------------------------------
// Pink noise: Voss-McCartney algorithm (8 octave bands)
// ---------------------------------------------------------------------------
struct PinkNoise {
    WhiteNoise white;
    float octave[8] = {};
    uint32_t count = 0;

    float next() {
        float sum = 0.0f;
        uint32_t last = count;
        count++;
        uint32_t changed = last ^ count;
        for (int i = 0; i < 8; i++) {
            if (changed & (1u << i))
                octave[i] = white.next();
            sum += octave[i];
        }
        return sum * 0.125f;
    }
};

// ---------------------------------------------------------------------------
// Phase-wrap trigger detection (delta < -0.5)
// ---------------------------------------------------------------------------
inline bool detect_trigger(float phase, float prev_phase) {
    return (phase - prev_phase) < -0.5f;
}

// ---------------------------------------------------------------------------
// 4-waveform generator: sine/saw/square/triangle from phase [0,1)
// ---------------------------------------------------------------------------
inline double waveform(double phase, int type) {
    switch (type) {
        case 0: // sine
            return std::sin(phase * 2.0 * M_PI);
        case 1: // saw (rising from -1 to +1)
            return 2.0 * phase - 1.0;
        case 2: // square
            return phase < 0.5 ? 1.0 : -1.0;
        case 3: // triangle
            return 4.0 * (phase < 0.5 ? phase : (1.0 - phase)) - 1.0;
        default:
            return 0.0;
    }
}

// ---------------------------------------------------------------------------
// 3-harmonic sine mixing: fundamental + 2nd (0.5) + 3rd (0.25)
// amount blends from pure sine (0) to full harmonic mix (1)
// ---------------------------------------------------------------------------
inline double harmonics_3(double phase, float amount) {
    double sine = std::sin(phase * 2.0 * M_PI);
    if (amount <= 0.0f) return sine;
    double h2 = std::sin(phase * 4.0 * M_PI) * 0.5;
    double h3 = std::sin(phase * 6.0 * M_PI) * 0.25;
    return sine * (1.0f - amount) + (sine + h2 + h3) * amount;
}

// ---------------------------------------------------------------------------
// Square-wave ring oscillator bank: N oscillators, returns normalized sum
// Advances phases in-place.
// ---------------------------------------------------------------------------
inline float ring_osc_bank(double* phases, const float* freqs, int count,
                           float pitch_mult, double inv_sr) {
    if (count <= 0) return 0.0f;
    float sum = 0.0f;
    for (int r = 0; r < count; r++) {
        sum += phases[r] < 0.5 ? 1.0f : -1.0f;
        phases[r] += freqs[r] * pitch_mult * inv_sr;
        if (phases[r] >= 1.0) phases[r] -= 1.0;
    }
    return sum / static_cast<float>(count);
}

} // namespace audio_dsp

#endif // VIVID_AUDIO_DSP_H
