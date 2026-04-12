#include "shared/filter_dsp/filter_dsp.h"

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
constexpr float kDenormalFloor = 1.0e-20f;

inline float flush_denormal(float x) {
    return std::fabs(x) < kDenormalFloor ? 0.0f : x;
}

} // namespace

// =============================================================================
// BiquadState
// =============================================================================

void BiquadState::reset() {
    z1[0] = z1[1] = z2[0] = z2[1] = 0.0f;
}

static BiquadCoeffs make_biquad_coeffs(float cutoff_hz, float reso, int ftype, float sr) {
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
            return {};
    }

    float inv_a0 = 1.0f / a0;
    return {b0 * inv_a0, b1 * inv_a0, b2 * inv_a0, a1 * inv_a0, a2 * inv_a0};
}

static float apply_prepared_biquad(BiquadState& st,
                                   float input,
                                   const BiquadCoeffs& c,
                                   bool cascade) {
    float out = c.b0 * input + st.z1[0];
    st.z1[0] = flush_denormal(c.b1 * input - c.a1 * out + st.z2[0]);
    st.z2[0] = flush_denormal(c.b2 * input - c.a2 * out);
    out = flush_denormal(out);

    if (cascade) {
        float in2 = out;
        out = c.b0 * in2 + st.z1[1];
        st.z1[1] = flush_denormal(c.b1 * in2 - c.a1 * out + st.z2[1]);
        st.z2[1] = flush_denormal(c.b2 * in2 - c.a2 * out);
        out = flush_denormal(out);
    }

    return out;
}

static float diode_clip(float x) {
    if (x > 0.0f) return std::tanh(x * 1.5f);
    return std::tanh(x * 0.8f);
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
    out = flush_denormal(out);
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
        stage[i] = flush_denormal(y + v);
        x = flush_denormal(y);
    }
    return flush_denormal(x);
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
        z1[b] = flush_denormal(-ca1 * y + z2[b]);  // b1=0 for bandpass
        z2[b] = flush_denormal(cb2 * input - ca2 * y);

        out += flush_denormal(y) * GAINS[b];
    }

    return flush_denormal(out);
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
        stage[i] = flush_denormal(y + v);
        x = flush_denormal(diode_clip(y));
    }

    feedback = flush_denormal(x);
    return flush_denormal(x);
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

    hp = flush_denormal(input - lp - fb);
    bp = flush_denormal(hp * f + s1);
    lp = flush_denormal(bp * f + s2);

    s1 = bp;
    s2 = lp;

    return flush_denormal(lp);
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

PreparedFilterPlan prepare_filter_plan(const FilterParams& params) {
    PreparedFilterPlan plan{};
    plan.type = params.type;
    plan.sample_rate = params.sample_rate;

    const float cutoff_hz = params.cutoff_hz;
    const float reso = params.resonance;
    const float sr = params.sample_rate;

    if (params.drive > 0.001f) {
        plan.drive_enabled = true;
        plan.drive_scale = 1.0f + params.drive * 7.0f;
        plan.drive_norm = 1.0f / std::tanh(plan.drive_scale);
    }

    switch (params.type) {
        case FILTER_LP12: case FILTER_LP24: case FILTER_HP12: case FILTER_HP24:
        case FILTER_BP:   case FILTER_NOTCH: case FILTER_PEAK: case FILTER_ALLPASS:
            plan.biquad = make_biquad_coeffs(cutoff_hz, reso, params.type, sr);
            plan.biquad_cascade = (params.type == FILTER_LP24 || params.type == FILTER_HP24);
            break;

        case FILTER_BP24: {
            float hp_cutoff = cutoff_hz * 0.8f;
            float rc = 1.0f / (kTwoPi * hp_cutoff);
            plan.biquad = make_biquad_coeffs(cutoff_hz * 1.2f, reso, FILTER_LP24, sr);
            plan.biquad_cascade = true;
            plan.bp24_hp_alpha = rc / (rc + 1.0f / sr);
            break;
        }

        case FILTER_COMB: {
            float delay_samples = std::clamp(sr / std::max(cutoff_hz, 20.0f),
                                             1.0f,
                                             static_cast<float>(CombFilterState::kMaxDelay - 1));
            plan.comb_delay_int = static_cast<int>(delay_samples);
            plan.comb_delay_frac = delay_samples - static_cast<float>(plan.comb_delay_int);
            plan.comb_feedback = std::clamp(reso * 0.98f, -0.98f, 0.98f);
            break;
        }

        case FILTER_LADDER: {
            float clamped = std::clamp(cutoff_hz, 20.0f, sr * 0.45f);
            float g = std::tan(kPi * clamped / sr);
            plan.ladder_g = g / (1.0f + g);
            plan.ladder_feedback = reso * 4.0f;
            break;
        }

        case FILTER_FORMANT: {
            float morph = std::log2(cutoff_hz / 20.0f) / std::log2(20000.0f / 20.0f);
            morph = std::clamp(morph, 0.0f, 1.0f);
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

            for (int b = 0; b < 3; ++b) {
                float freq = FORMANTS[idx][b] * (1.0f - frac) + FORMANTS[idx + 1][b] * frac;
                freq = std::min(freq, sr * 0.45f);

                float omega = kTwoPi * freq / sr;
                float sin_w = std::sin(omega);
                float cos_w = std::cos(omega);
                float alpha = sin_w / (2.0f * Q);

                float cb0 = sin_w * 0.5f;
                float cb2 = -sin_w * 0.5f;
                float ca0 = 1.0f + alpha;
                float ca1 = -2.0f * cos_w;
                float ca2 = 1.0f - alpha;

                float inv_a0 = 1.0f / ca0;
                plan.formant[b].b0 = cb0 * inv_a0;
                plan.formant[b].b2 = cb2 * inv_a0;
                plan.formant[b].a1 = ca1 * inv_a0;
                plan.formant[b].a2 = ca2 * inv_a0;
                plan.formant[b].gain = GAINS[b];
            }
            break;
        }

        case FILTER_DIODE: {
            float clamped = std::clamp(cutoff_hz, 20.0f, sr * 0.45f);
            float g = std::tan(kPi * clamped / sr);
            plan.diode_g = g / (1.0f + g);
            plan.diode_feedback = reso * 4.5f;
            break;
        }

        case FILTER_MS20: {
            float clamped = std::clamp(cutoff_hz, 20.0f, sr * 0.45f);
            float f = 2.0f * std::sin(kPi * clamped / sr);
            plan.ms20_f = std::clamp(f, 0.0f, 1.0f);
            plan.ms20_feedback_scale = reso * 2.0f;
            break;
        }

        default:
            break;
    }

    return plan;
}

float FilterState::process_prepared(float input, const PreparedFilterPlan& plan) {
    if (plan.drive_enabled)
        input = std::tanh(input * plan.drive_scale) * plan.drive_norm;

    switch (plan.type) {
        case FILTER_LP12: case FILTER_LP24: case FILTER_HP12: case FILTER_HP24:
        case FILTER_BP:   case FILTER_NOTCH: case FILTER_PEAK: case FILTER_ALLPASS:
            return apply_prepared_biquad(biquad, input, plan.biquad, plan.biquad_cascade);

        case FILTER_BP24: {
            float lp = apply_prepared_biquad(biquad, input, plan.biquad, true);
            float hp_out = plan.bp24_hp_alpha * (biquad.z2[1] + lp - biquad.z1[1]);
            biquad.z1[1] = flush_denormal(lp);
            biquad.z2[1] = flush_denormal(hp_out);
            return flush_denormal(hp_out);
        }

        case FILTER_COMB: {
            int read0 = (comb.write_pos - plan.comb_delay_int + CombFilterState::kMaxDelay)
                      % CombFilterState::kMaxDelay;
            int read1 = (read0 - 1 + CombFilterState::kMaxDelay) % CombFilterState::kMaxDelay;

            float delayed = comb.buffer[read0] * (1.0f - plan.comb_delay_frac)
                          + comb.buffer[read1] * plan.comb_delay_frac;

            float out = flush_denormal(input + delayed * plan.comb_feedback);
            comb.buffer[comb.write_pos] = out;
            comb.write_pos = (comb.write_pos + 1) % CombFilterState::kMaxDelay;
            return out;
        }

        case FILTER_LADDER: {
            float x = std::tanh(input - plan.ladder_feedback * ladder.stage[3]);
            for (int i = 0; i < 4; ++i) {
                float v = (x - ladder.stage[i]) * plan.ladder_g;
                float y = v + ladder.stage[i];
                ladder.stage[i] = flush_denormal(y + v);
                x = flush_denormal(y);
            }
            return flush_denormal(x);
        }

        case FILTER_FORMANT: {
            float out = 0.0f;
            for (int b = 0; b < 3; ++b) {
                const auto& c = plan.formant[b];
                float y = c.b0 * input + formant.z1[b];
                formant.z1[b] = flush_denormal(-c.a1 * y + formant.z2[b]);
                formant.z2[b] = flush_denormal(c.b2 * input - c.a2 * y);
                out += flush_denormal(y) * c.gain;
            }
            return flush_denormal(out);
        }

        case FILTER_DIODE: {
            float x = diode_clip(input - plan.diode_feedback * diode.feedback);
            for (int i = 0; i < 4; ++i) {
                float v = (x - diode.stage[i]) * plan.diode_g;
                float y = v + diode.stage[i];
                diode.stage[i] = flush_denormal(y + v);
                x = flush_denormal(diode_clip(y));
            }
            diode.feedback = flush_denormal(x);
            return flush_denormal(x);
        }

        case FILTER_MS20: {
            float fb = std::tanh(plan.ms20_feedback_scale * ms20.bp);
            ms20.hp = flush_denormal(input - ms20.lp - fb);
            ms20.bp = flush_denormal(ms20.hp * plan.ms20_f + ms20.s1);
            ms20.lp = flush_denormal(ms20.bp * plan.ms20_f + ms20.s2);
            ms20.s1 = ms20.bp;
            ms20.s2 = ms20.lp;
            return flush_denormal(ms20.lp);
        }

        default:
            return input;
    }
}

float FilterState::process(float input, float cutoff_hz, float reso, float drive,
                           int ftype, float sr) {
    FilterParams params{};
    params.type = ftype;
    params.cutoff_hz = cutoff_hz;
    params.resonance = reso;
    params.drive = drive;
    params.sample_rate = sr;
    const PreparedFilterPlan plan = prepare_filter_plan(params);
    return process_prepared(input, plan);
}

void process_filter_block(FilterState& state,
                          const PreparedFilterPlan& plan,
                          const float* input,
                          float* output,
                          uint32_t frames) {
    for (uint32_t i = 0; i < frames; ++i)
        output[i] = state.process_prepared(input[i], plan);
}

} // namespace audio_dsp
