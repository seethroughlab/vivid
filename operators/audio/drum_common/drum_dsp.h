#ifndef VIVID_DRUM_DSP_H
#define VIVID_DRUM_DSP_H

#include <cmath>
#include <cstdint>
#include "operator_api/audio_dsp.h"

namespace drum {

using audio_dsp::WhiteNoise;
using audio_dsp::detect_trigger;

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
// Exponential decay envelope: exp(-t * 5.0 / decay_seconds)
// ---------------------------------------------------------------------------
struct DecayEnvelope {
    double time = 1000.0; // large = silence at startup

    void trigger() { time = 0.0; }
    void advance(double inv_sr) { time += inv_sr; }

    float value(float decay_seconds) const {
        return static_cast<float>(std::exp(-time * 5.0 / decay_seconds));
    }
};

// ---------------------------------------------------------------------------
// Chamberlin state-variable filter (LP/HP/BP)
// f = min(2*sin(pi*freq/sr), 0.95) for stability
// ---------------------------------------------------------------------------
struct SVF {
    enum Mode { LP, HP, BP };

    float low  = 0.0f;
    float high = 0.0f;
    float band = 0.0f;

    void reset() { low = high = band = 0.0f; }

    float process(float input, float cutoff_hz, float resonance, float sample_rate, Mode mode) {
        float f = 2.0f * std::sin(static_cast<float>(M_PI) * cutoff_hz / sample_rate);
        if (f > 0.95f) f = 0.95f;
        float q = 1.0f - resonance;
        if (q < 0.05f) q = 0.05f;

        low  += f * band;
        high  = input - low - q * band;
        band += f * high;

        switch (mode) {
            case LP: return low;
            case HP: return high;
            case BP: return band;
        }
        return low;
    }
};

// ---------------------------------------------------------------------------
// Soft clipper: tanh(x * (1 + drive * 3))
// ---------------------------------------------------------------------------
inline float soft_clip(float x, float drive) {
    return std::tanh(x * (1.0f + drive * 3.0f));
}

} // namespace drum

#endif // VIVID_DRUM_DSP_H
