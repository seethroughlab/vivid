#include "operator_api/filter_dsp.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio_dsp {
namespace {

constexpr float kPi = static_cast<float>(M_PI);
constexpr float kTwoPi = 2.0f * kPi;

} // namespace

// =============================================================================
// BiquadState
// =============================================================================

void BiquadState::reset() {
    z1[0] = z1[1] = z2[0] = z2[1] = 0.0f;
}

static float apply_biquad(BiquadState& st, float input, float cutoff_hz, float reso,
                           int ftype, float sr) {
    cutoff_hz = std::clamp(cutoff_hz, 20.0f, sr * 0.45f);
    reso = std::clamp(reso, 0.0f, 1.0f);

    float omega = kTwoPi * cutoff_hz / sr;
    float sin_w = std::sin(omega);
    float cos_w = std::cos(omega);
    float Q     = 0.5f + reso * 19.5f;
    float alpha = sin_w / (2.0f * Q);

    float b0, b1, b2, a0, a1, a2;

    switch (ftype) {
        case FILTER_LP12: case FILTER_LP24:
            b0 = (1.0f - cos_w) * 0.5f;
            b1 =  1.0f - cos_w;
            b2 = (1.0f - cos_w) * 0.5f;
            a0 =  1.0f + alpha;
            a1 = -2.0f * cos_w;
            a2 =  1.0f - alpha;
            break;
        case FILTER_HP12: case FILTER_HP24:
            b0 = (1.0f + cos_w) * 0.5f;
            b1 = -(1.0f + cos_w);
            b2 = (1.0f + cos_w) * 0.5f;
            a0 =  1.0f + alpha;
            a1 = -2.0f * cos_w;
            a2 =  1.0f - alpha;
            break;
        case FILTER_BP:
            b0 =  sin_w * 0.5f;
            b1 =  0.0f;
            b2 = -sin_w * 0.5f;
            a0 =  1.0f + alpha;
            a1 = -2.0f * cos_w;
            a2 =  1.0f - alpha;
            break;
        case FILTER_NOTCH:
            b0 =  1.0f;
            b1 = -2.0f * cos_w;
            b2 =  1.0f;
            a0 =  1.0f + alpha;
            a1 = -2.0f * cos_w;
            a2 =  1.0f - alpha;
            break;
        case FILTER_PEAK: {
            float A = std::pow(10.0f, reso * 12.0f / 40.0f);
            float alpha_pk = sin_w / (2.0f * std::max(Q, 0.5f));
            b0 =  1.0f + alpha_pk * A;
            b1 = -2.0f * cos_w;
            b2 =  1.0f - alpha_pk * A;
            a0 =  1.0f + alpha_pk / A;
            a1 = -2.0f * cos_w;
            a2 =  1.0f - alpha_pk / A;
            break;
        }
        case FILTER_ALLPASS:
            b0 =  1.0f - alpha;
            b1 = -2.0f * cos_w;
            b2 =  1.0f + alpha;
            a0 =  1.0f + alpha;
            a1 = -2.0f * cos_w;
            a2 =  1.0f - alpha;
            break;
        default:
            return input;
    }

    float inv_a0 = 1.0f / a0;
    b0 *= inv_a0; b1 *= inv_a0; b2 *= inv_a0;
    a1 *= inv_a0; a2 *= inv_a0;

    // Stage 1 (transposed direct form II)
    float out = b0 * input + st.z1[0];
    st.z1[0] = b1 * input - a1 * out + st.z2[0];
    st.z2[0] = b2 * input - a2 * out;

    // Stage 2 for 4-pole (LP24, HP24)
    if (ftype == FILTER_LP24 || ftype == FILTER_HP24) {
        float in2 = out;
        out = b0 * in2 + st.z1[1];
        st.z1[1] = b1 * in2 - a1 * out + st.z2[1];
        st.z2[1] = b2 * in2 - a2 * out;
    }

    return out;
}

// =============================================================================
// CombFilterState
// =============================================================================

void CombFilterState::reset() {
    std::memset(buffer, 0, sizeof(buffer));
    write_pos = 0;
}

float CombFilterState::process(float input, float delay_samples, float feedback) {
    delay_samples = std::clamp(delay_samples, 1.0f, static_cast<float>(kMaxDelay - 1));
    feedback = std::clamp(feedback, -0.98f, 0.98f);

    int d_int = static_cast<int>(delay_samples);
    float d_frac = delay_samples - static_cast<float>(d_int);

    int read0 = (write_pos - d_int + kMaxDelay) % kMaxDelay;
    int read1 = (read0 - 1 + kMaxDelay) % kMaxDelay;

    float delayed = buffer[read0] * (1.0f - d_frac) + buffer[read1] * d_frac;

    float out = input + delayed * feedback;
    buffer[write_pos] = out;
    write_pos = (write_pos + 1) % kMaxDelay;
    return out;
}

// =============================================================================
// LadderFilterState
// =============================================================================

void LadderFilterState::reset() {
    stage[0] = stage[1] = stage[2] = stage[3] = 0.0f;
}

float LadderFilterState::process(float input, float cutoff_hz, float reso, float sample_rate) {
    cutoff_hz = std::clamp(cutoff_hz, 20.0f, sample_rate * 0.45f);
    float g = std::tan(kPi * cutoff_hz / sample_rate);
    float fb = reso * 4.0f;
    float x = std::tanh(input - fb * stage[3]);

    for (int i = 0; i < 4; ++i) {
        float v = (x - stage[i]) * g / (1.0f + g);
        float y = v + stage[i];
        stage[i] = y + v;
        x = y;
    }
    return x;
}

// =============================================================================
// FormantFilterState
// =============================================================================

void FormantFilterState::reset() {
    std::memset(z1, 0, sizeof(z1));
    std::memset(z2, 0, sizeof(z2));
}

float FormantFilterState::process(float input, float morph, float reso, float sample_rate) {
    static constexpr float FORMANTS[5][3] = {
        {800.0f, 1150.0f, 2900.0f},
        {350.0f, 2000.0f, 2800.0f},
        {270.0f, 2300.0f, 3000.0f},
        {450.0f, 800.0f, 2830.0f},
        {325.0f, 700.0f, 2530.0f},
    };
    static constexpr float GAINS[3] = {1.0f, 0.5f, 0.25f};

    float pos = morph * 4.0f;
    int idx = std::min(static_cast<int>(pos), 3);
    float frac = pos - static_cast<float>(idx);

    float Q = 1.0f + reso * 19.0f;
    float out = 0.0f;

    for (int b = 0; b < 3; ++b) {
        float freq = FORMANTS[idx][b] * (1.0f - frac) + FORMANTS[idx + 1][b] * frac;
        freq = std::min(freq, sample_rate * 0.45f);

        float omega = kTwoPi * freq / sample_rate;
        float sin_w = std::sin(omega);
        float cos_w = std::cos(omega);
        float alpha = sin_w / (2.0f * Q);

        float cb0 = sin_w * 0.5f;
        float cb2 = -sin_w * 0.5f;
        float ca0 = 1.0f + alpha;
        float ca1 = -2.0f * cos_w;
        float ca2 = 1.0f - alpha;

        float inv_a0 = 1.0f / ca0;
        cb0 *= inv_a0; cb2 *= inv_a0;
        ca1 *= inv_a0; ca2 *= inv_a0;

        float y = cb0 * input + z1[b];
        z1[b] = -ca1 * y + z2[b];  // b1=0 for bandpass
        z2[b] = cb2 * input - ca2 * y;

        out += y * GAINS[b];
    }

    return out;
}

// =============================================================================
// DiodeLadderState
// =============================================================================

void DiodeLadderState::reset() {
    stage[0] = stage[1] = stage[2] = stage[3] = 0.0f;
    feedback = 0.0f;
}

float DiodeLadderState::process(float input, float cutoff_hz, float reso, float sample_rate) {
    cutoff_hz = std::clamp(cutoff_hz, 20.0f, sample_rate * 0.45f);
    float g = std::tan(kPi * cutoff_hz / sample_rate);
    float fb = reso * 4.5f;

    auto diode_clip = [](float x) -> float {
        if (x > 0.0f) return std::tanh(x * 1.5f);
        return std::tanh(x * 0.8f);
    };

    float x = diode_clip(input - fb * feedback);

    for (int i = 0; i < 4; ++i) {
        float v = (x - stage[i]) * g / (1.0f + g);
        float y = v + stage[i];
        stage[i] = y + v;
        x = diode_clip(y);
    }

    feedback = x;
    return x;
}

// =============================================================================
// MS20FilterState
// =============================================================================

void MS20FilterState::reset() {
    hp = bp = lp = s1 = s2 = 0.0f;
}

float MS20FilterState::process(float input, float cutoff_hz, float reso, float sample_rate) {
    cutoff_hz = std::clamp(cutoff_hz, 20.0f, sample_rate * 0.45f);
    float f = 2.0f * std::sin(kPi * cutoff_hz / sample_rate);
    f = std::clamp(f, 0.0f, 1.0f);
    float k = reso * 2.0f;

    float fb = std::tanh(k * bp);

    hp = input - lp - fb;
    bp = hp * f + s1;
    lp = bp * f + s2;

    s1 = bp;
    s2 = lp;

    return lp;
}

// =============================================================================
// FilterState — unified dispatch
// =============================================================================

void FilterState::reset() {
    biquad.reset();
    comb.reset();
    ladder.reset();
    formant.reset();
    diode.reset();
    ms20.reset();
}

float FilterState::process(float input, float cutoff_hz, float reso, float drive,
                           int ftype, float sr) {
    // Drive: pre-filter soft-clip
    if (drive > 0.001f) {
        float d = 1.0f + drive * 7.0f;
        input = std::tanh(input * d) / std::tanh(d);
    }

    switch (ftype) {
        case FILTER_LP12: case FILTER_LP24: case FILTER_HP12: case FILTER_HP24:
        case FILTER_BP:   case FILTER_NOTCH: case FILTER_PEAK: case FILTER_ALLPASS:
            return apply_biquad(biquad, input, cutoff_hz, reso, ftype, sr);

        case FILTER_BP24: {
            // Tight bandpass: LP24 then 1-pole HP in series
            float lp = apply_biquad(biquad, input, cutoff_hz * 1.2f, reso, FILTER_LP24, sr);
            float hp_cutoff = cutoff_hz * 0.8f;
            float rc = 1.0f / (kTwoPi * hp_cutoff);
            float alpha_hp = rc / (rc + 1.0f / sr);
            float hp_out = alpha_hp * (biquad.z2[1] + lp - biquad.z1[1]);
            biquad.z1[1] = lp;
            biquad.z2[1] = hp_out;
            return hp_out;
        }

        case FILTER_COMB: {
            float delay_samples = sr / std::max(cutoff_hz, 20.0f);
            float feedback = reso * 0.98f;
            return comb.process(input, delay_samples, feedback);
        }

        case FILTER_LADDER:
            return ladder.process(input, cutoff_hz, reso, sr);

        case FILTER_FORMANT: {
            float morph = std::log2(cutoff_hz / 20.0f) / std::log2(20000.0f / 20.0f);
            morph = std::clamp(morph, 0.0f, 1.0f);
            return formant.process(input, morph, reso, sr);
        }

        case FILTER_DIODE:
            return diode.process(input, cutoff_hz, reso, sr);

        case FILTER_MS20:
            return ms20.process(input, cutoff_hz, reso, sr);

        default:
            return input;
    }
}

} // namespace audio_dsp
