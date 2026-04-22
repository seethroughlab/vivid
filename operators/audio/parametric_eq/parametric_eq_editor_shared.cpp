#include "parametric_eq_editor_shared.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace vivid::parametric_eq_editor {

const char* band_type_name(int type) {
    switch (type) {
        case kPeak:      return "Peak";
        case kLowShelf:  return "Low Shelf";
        case kHighShelf: return "High Shelf";
        case kLowPass:   return "Low Pass";
        case kHighPass:  return "High Pass";
        default:         return "?";
    }
}

float freq_to_fraction(float hz) {
    const float min_l = std::log2(kMinFreqHz);
    const float max_l = std::log2(kMaxFreqHz);
    const float hzc   = std::clamp(hz, kMinFreqHz, kMaxFreqHz);
    return (std::log2(hzc) - min_l) / (max_l - min_l);
}

float fraction_to_freq(float frac) {
    const float min_l = std::log2(kMinFreqHz);
    const float max_l = std::log2(kMaxFreqHz);
    const float f     = std::clamp(frac, 0.0f, 1.0f);
    return std::pow(2.0f, min_l + f * (max_l - min_l));
}

float db_to_fraction(float db) {
    const float dbc = std::clamp(db, kMinGainDb, kMaxGainDb);
    return (dbc - kMinGainDb) / (kMaxGainDb - kMinGainDb);
}

float fraction_to_db(float frac) {
    const float f = std::clamp(frac, 0.0f, 1.0f);
    return kMinGainDb + f * (kMaxGainDb - kMinGainDb);
}

BiquadCoeffs compute_coeffs(int type, float freq_hz, float gain_db,
                            float Q, float sample_rate) {
    // Guard against denormals / bad inputs so the editor never NaNs out.
    const float sr   = std::max(1.0f, sample_rate);
    const float freq = std::clamp(freq_hz, 1.0f, sr * 0.499f);
    const float q    = std::max(0.01f, Q);

    const float w0     = 2.0f * static_cast<float>(M_PI) * freq / sr;
    const float cos_w0 = std::cos(w0);
    const float sin_w0 = std::sin(w0);
    const float alpha  = sin_w0 / (2.0f * q);
    const float A      = std::pow(10.0f, gain_db / 40.0f);

    float b0 = 0, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

    switch (type) {
        default:
        case kPeak: {
            b0 = 1.0f + alpha * A;
            b1 = -2.0f * cos_w0;
            b2 = 1.0f - alpha * A;
            a0 = 1.0f + alpha / A;
            a1 = -2.0f * cos_w0;
            a2 = 1.0f - alpha / A;
            break;
        }
        case kLowShelf: {
            const float sq = 2.0f * std::sqrt(A) * alpha;
            b0 = A * ((A + 1.0f) - (A - 1.0f) * cos_w0 + sq);
            b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cos_w0);
            b2 = A * ((A + 1.0f) - (A - 1.0f) * cos_w0 - sq);
            a0 = (A + 1.0f) + (A - 1.0f) * cos_w0 + sq;
            a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cos_w0);
            a2 = (A + 1.0f) + (A - 1.0f) * cos_w0 - sq;
            break;
        }
        case kHighShelf: {
            const float sq = 2.0f * std::sqrt(A) * alpha;
            b0 = A * ((A + 1.0f) + (A - 1.0f) * cos_w0 + sq);
            b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cos_w0);
            b2 = A * ((A + 1.0f) + (A - 1.0f) * cos_w0 - sq);
            a0 = (A + 1.0f) - (A - 1.0f) * cos_w0 + sq;
            a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cos_w0);
            a2 = (A + 1.0f) - (A - 1.0f) * cos_w0 - sq;
            break;
        }
        case kLowPass: {
            b0 = (1.0f - cos_w0) / 2.0f;
            b1 = 1.0f - cos_w0;
            b2 = (1.0f - cos_w0) / 2.0f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cos_w0;
            a2 = 1.0f - alpha;
            break;
        }
        case kHighPass: {
            b0 = (1.0f + cos_w0) / 2.0f;
            b1 = -(1.0f + cos_w0);
            b2 = (1.0f + cos_w0) / 2.0f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cos_w0;
            a2 = 1.0f - alpha;
            break;
        }
    }

    const float inv_a0 = 1.0f / a0;
    return {b0 * inv_a0, b1 * inv_a0, b2 * inv_a0, a1 * inv_a0, a2 * inv_a0};
}

float band_magnitude(const BiquadCoeffs& c, float eval_hz, float sample_rate) {
    const float sr = std::max(1.0f, sample_rate);
    const float w  = 2.0f * static_cast<float>(M_PI) *
                     std::clamp(eval_hz, 1.0f, sr * 0.499f) / sr;
    // |H(e^jω)| = |b0 + b1 e^-jω + b2 e^-j2ω| / |1 + a1 e^-jω + a2 e^-j2ω|
    const float c1 = std::cos(w);
    const float c2 = std::cos(2.0f * w);
    const float s1 = std::sin(w);
    const float s2 = std::sin(2.0f * w);

    const float num_re = c.b0 + c.b1 * c1 + c.b2 * c2;
    const float num_im = -(c.b1 * s1 + c.b2 * s2);
    const float den_re = 1.0f + c.a1 * c1 + c.a2 * c2;
    const float den_im = -(c.a1 * s1 + c.a2 * s2);

    const float num = std::sqrt(num_re * num_re + num_im * num_im);
    const float den = std::sqrt(den_re * den_re + den_im * den_im);
    if (den < 1e-12f) return 0.0f;
    return num / den;
}

float band_magnitude_db(int type, float freq_hz, float gain_db,
                        float Q, float sample_rate, float eval_hz) {
    BiquadCoeffs c = compute_coeffs(type, freq_hz, gain_db, Q, sample_rate);
    const float mag = band_magnitude(c, eval_hz, sample_rate);
    if (mag <= 1e-6f) return kMinGainDb;
    return 20.0f * std::log10(mag);
}

float composite_magnitude_db(const int* types, const float* freqs,
                             const float* gains, const float* Qs,
                             int active_band_count,
                             float sample_rate, float eval_hz) {
    if (!types || !freqs || !gains || !Qs) return 0.0f;
    const int n = std::clamp(active_band_count, 0, kMaxBands);
    float total = 0.0f;
    for (int b = 0; b < n; ++b) {
        total += band_magnitude_db(types[b], freqs[b], gains[b], Qs[b],
                                   sample_rate, eval_hz);
    }
    return std::clamp(total, kMinGainDb * 2.0f, kMaxGainDb * 2.0f);
}

NodePoint band_node_position(float plane_x, float plane_y,
                             float plane_w, float plane_h,
                             float freq_hz, float gain_db) {
    NodePoint p{};
    p.x = plane_x + freq_to_fraction(freq_hz) * plane_w;
    // In screen space, y increases downward, so invert the dB fraction.
    p.y = plane_y + (1.0f - db_to_fraction(gain_db)) * plane_h;
    return p;
}

int hit_test_band(float plane_x, float plane_y,
                  float plane_w, float plane_h,
                  float mouse_x, float mouse_y,
                  const float* freqs, const float* gains,
                  int active_band_count, float hit_radius_px) {
    if (!freqs || !gains) return -1;
    const int n = std::clamp(active_band_count, 0, kMaxBands);
    int best = -1;
    float best_d = hit_radius_px * hit_radius_px;
    for (int b = 0; b < n; ++b) {
        NodePoint p = band_node_position(plane_x, plane_y, plane_w, plane_h,
                                         freqs[b], gains[b]);
        const float dx = mouse_x - p.x;
        const float dy = mouse_y - p.y;
        const float d  = dx * dx + dy * dy;
        if (d < best_d) {
            best_d = d;
            best   = b;
        }
    }
    return best;
}

} // namespace vivid::parametric_eq_editor
