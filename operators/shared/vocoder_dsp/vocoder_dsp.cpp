#include "shared/vocoder_dsp/vocoder_dsp.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace vivid::vocoder_dsp {
namespace {

float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

int clamp_bands(int bands) {
    return std::max(4, std::min(kMaxBands, bands));
}

} // namespace

const char* backend_name(Backend backend) {
    switch (backend) {
        case Backend::Scalar:
        default: return "scalar";
    }
}

Backend preferred_backend() {
    return Backend::Scalar;
}

void Engine::reset() {
    clear_band_state();
    coeff_sample_rate_ = 0;
    coeff_bands_ = 0;
    norm_ = 1.0f;
    total_coefficient_rebuilds_ = 0;
    last_stats_ = {};
}

void Engine::clear_band_state() {
    mod_low_.fill(0.0f);
    mod_band_.fill(0.0f);
    car_low_.fill(0.0f);
    car_band_.fill(0.0f);
    envelope_.fill(0.0f);
}

void Engine::ensure_coefficients(uint32_t sample_rate, int bands) {
    const int num_bands = clamp_bands(bands);
    if (coeff_sample_rate_ == sample_rate && coeff_bands_ == num_bands)
        return;

    const float sr = static_cast<float>(sample_rate);
    for (int b = 0; b < num_bands; ++b) {
        if (num_bands > 1) {
            band_freqs_[b] = 80.0f * std::pow(12000.0f / 80.0f,
                                              static_cast<float>(b) / static_cast<float>(num_bands - 1));
        } else {
            band_freqs_[b] = 1000.0f;
        }
    }

    for (int b = 0; b < num_bands; ++b) {
        float f = 2.0f * std::sin(static_cast<float>(M_PI) * band_freqs_[b] / sr);
        if (f > 0.95f) f = 0.95f;
        band_f_[b] = f;

        if (num_bands > 2) {
            const float lo = (b > 0) ? band_freqs_[b - 1] : band_freqs_[0] * 0.5f;
            const float hi = (b < num_bands - 1) ? band_freqs_[b + 1] : band_freqs_[num_bands - 1] * 2.0f;
            band_q_[b] = 1.0f / (band_freqs_[b] / (hi - lo));
        } else {
            band_q_[b] = 0.15f;
        }
        band_q_[b] = clampf(band_q_[b], 0.05f, 0.5f);
    }

    norm_ = 1.0f / std::sqrt(static_cast<float>(num_bands));
    coeff_sample_rate_ = sample_rate;
    coeff_bands_ = num_bands;
    ++total_coefficient_rebuilds_;
}

void Engine::process(const float* modulator,
                     const float* carrier,
                     float* out,
                     uint32_t frames,
                     uint32_t sample_rate,
                     const ProcessParams& params,
                     Backend backend) {
    if (!modulator || !carrier || !out || frames == 0 || sample_rate == 0)
        return;

    const int num_bands = clamp_bands(params.bands);
    const int rebuilds_before = total_coefficient_rebuilds_;
    ensure_coefficients(sample_rate, num_bands);

    const float speed_ms = clampf(params.envelope_speed_ms, 1.0f, 500.0f);
    const float wet = clampf(params.mix, 0.0f, 1.0f);
    const float dry = 1.0f - wet;
    const float sr = static_cast<float>(sample_rate);
    const float env_coeff = 1.0f - std::exp(-1.0f / (speed_ms * 0.001f * sr));

    for (uint32_t i = 0; i < frames; ++i) {
        const float mod_sample = modulator[i];
        const float car_sample = carrier[i];
        float band_sum = 0.0f;

        for (int b = 0; b < num_bands; ++b) {
            const float f = band_f_[b];
            const float q = band_q_[b];

            mod_low_[b] += f * mod_band_[b];
            const float mod_high = mod_sample - mod_low_[b] - q * mod_band_[b];
            mod_band_[b] += f * mod_high;
            const float mod_bp = mod_band_[b];

            const float abs_mod = std::fabs(mod_bp);
            envelope_[b] += (abs_mod - envelope_[b]) * env_coeff;

            car_low_[b] += f * car_band_[b];
            const float car_high = car_sample - car_low_[b] - q * car_band_[b];
            car_band_[b] += f * car_high;
            const float car_bp = car_band_[b];

            band_sum += car_bp * envelope_[b];
        }

        const float wet_sig = band_sum * norm_;
        out[i] = mod_sample * dry + wet_sig * wet;
    }

    last_stats_.backend = backend;
    last_stats_.bands = num_bands;
    last_stats_.coefficient_rebuilds = total_coefficient_rebuilds_ - rebuilds_before;
}

} // namespace vivid::vocoder_dsp
