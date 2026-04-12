#pragma once

#include <array>
#include <cstdint>

namespace vivid::vocoder_dsp {

static constexpr int kMaxBands = 32;

enum class Backend {
    Scalar,
};

const char* backend_name(Backend backend);
Backend preferred_backend();

struct ProcessParams {
    int bands = 16;
    float envelope_speed_ms = 50.0f;
    float mix = 1.0f;
};

struct ProcessStats {
    Backend backend = Backend::Scalar;
    int bands = 16;
    int coefficient_rebuilds = 0;
};

class Engine {
public:
    void reset();
    void process(const float* modulator,
                 const float* carrier,
                 float* out,
                 uint32_t frames,
                 uint32_t sample_rate,
                 const ProcessParams& params,
                 Backend backend = preferred_backend());

    ProcessStats last_stats() const { return last_stats_; }
    int total_coefficient_rebuilds() const { return total_coefficient_rebuilds_; }

private:
    void ensure_coefficients(uint32_t sample_rate, int bands);
    void clear_band_state();

    std::array<float, kMaxBands> mod_low_{};
    std::array<float, kMaxBands> mod_band_{};
    std::array<float, kMaxBands> car_low_{};
    std::array<float, kMaxBands> car_band_{};
    std::array<float, kMaxBands> envelope_{};
    std::array<float, kMaxBands> band_freqs_{};
    std::array<float, kMaxBands> band_f_{};
    std::array<float, kMaxBands> band_q_{};

    uint32_t coeff_sample_rate_ = 0;
    int coeff_bands_ = 0;
    float norm_ = 1.0f;
    int total_coefficient_rebuilds_ = 0;
    ProcessStats last_stats_{};
};

} // namespace vivid::vocoder_dsp
