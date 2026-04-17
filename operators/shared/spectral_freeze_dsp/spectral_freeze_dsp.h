#pragma once

#include "operator_api/audio_dsp.h"
#include "runtime/simd/fft.h"
#include "runtime/simd/simd_config.h"

#include <cstdint>
#include <vector>

namespace vivid::spectral_freeze_dsp {

enum class Backend {
    Scalar,
    Accelerate,
};

const char* backend_name(Backend backend);
Backend preferred_backend();
int resolve_fft_size(int param_index);

class Engine {
public:
    Engine() = default;
    ~Engine() = default;

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void process(const float* in,
                 float* out,
                 uint32_t frames,
                 uint32_t sample_rate,
                 int fft_size_param,
                 float freeze,
                 float blend,
                 float smoothing,
                 int phase_mode,
                 Backend backend = preferred_backend());

    void reset();
    Backend last_backend() const { return last_backend_; }

private:
    void lazy_init(uint32_t sample_rate, int fft_size_param);
    void process_fft_frame_scalar(int N, float freeze, float blend, float smoothing, int phase_mode);
    bool process_fft_frame_accelerate(int N, float freeze, float blend, float smoothing, int phase_mode);

    std::vector<float> input_ring_;
    std::vector<float> output_accum_;
    std::vector<float> hann_window_;
    std::vector<float> fft_real_;
    std::vector<float> fft_imag_;
    std::vector<float> mag_buf_;
    std::vector<float> phase_buf_;
    std::vector<float> frozen_mag_;
    std::vector<float> frozen_phase_;
    std::vector<float> smoothed_mag_;

    std::vector<float> cos_buf_;
    std::vector<float> sin_buf_;
    std::vector<float> scratch_;

    vivid::simd::FftPlanCache fft_cache_;

    int ring_pos_ = 0;
    int hop_counter_ = 0;
    bool spectrum_captured_ = false;
    bool prev_frozen_ = false;
    bool initialized_ = false;
    uint32_t init_rate_ = 0;
    int init_fft_ = -1;
    Backend last_backend_ = Backend::Scalar;

    audio_dsp::WhiteNoise rng_;
};

} // namespace vivid::spectral_freeze_dsp
