#include "shared/spectral_freeze_dsp/spectral_freeze_dsp.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace vivid::spectral_freeze_dsp {
namespace {

static float clamp01(float v) {
    return std::max(0.0f, std::min(1.0f, v));
}

} // namespace

const char* backend_name(Backend backend) {
    switch (backend) {
        case Backend::Accelerate: return "accelerate";
        case Backend::Scalar:
        default:                  return "scalar";
    }
}

Backend preferred_backend() {
#if VIVID_ACCELERATE_ENABLED
    return Backend::Accelerate;
#else
    return Backend::Scalar;
#endif
}

int resolve_fft_size(int param_index) {
    switch (param_index) {
        case 0: return 256;
        case 1: return 512;
        case 2: return 1024;
        default: return 512;
    }
}

void Engine::reset() {
    initialized_ = false;
    init_rate_ = 0;
    init_fft_ = -1;
    ring_pos_ = 0;
    hop_counter_ = 0;
    spectrum_captured_ = false;
    prev_frozen_ = false;
    input_ring_.clear();
    output_accum_.clear();
    hann_window_.clear();
    fft_real_.clear();
    fft_imag_.clear();
    mag_buf_.clear();
    phase_buf_.clear();
    frozen_mag_.clear();
    frozen_phase_.clear();
    smoothed_mag_.clear();
    cos_buf_.clear();
    sin_buf_.clear();
    scratch_.clear();
    fft_cache_.clear();
}

void Engine::lazy_init(uint32_t sample_rate, int fft_size_param) {
    const int N = resolve_fft_size(fft_size_param);
    if (initialized_ && init_rate_ == sample_rate && init_fft_ == N) return;

    input_ring_.assign(N, 0.0f);
    output_accum_.assign(2 * N, 0.0f);
    hann_window_.resize(N);
    for (int i = 0; i < N; ++i) {
        hann_window_[i] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * i / (N - 1)));
    }

    fft_real_.assign(N, 0.0f);
    fft_imag_.assign(N, 0.0f);
    cos_buf_.assign(N / 2 + 1, 0.0f);
    sin_buf_.assign(N / 2 + 1, 0.0f);
    scratch_.assign(N, 0.0f);

    const int bins = N / 2 + 1;
    mag_buf_.assign(bins, 0.0f);
    phase_buf_.assign(bins, 0.0f);
    frozen_mag_.assign(bins, 0.0f);
    frozen_phase_.assign(bins, 0.0f);
    smoothed_mag_.assign(bins, 0.0f);

    fft_cache_.clear();
    fft_cache_.reserve(static_cast<uint32_t>(N));

    ring_pos_ = 0;
    hop_counter_ = 0;
    spectrum_captured_ = false;
    prev_frozen_ = false;
    initialized_ = true;
    init_rate_ = sample_rate;
    init_fft_ = N;
}

void Engine::process_fft_frame_scalar(int N, float freeze, float blend, float smoothing, int phase_mode) {
    const int bins = N / 2 + 1;

    for (int i = 0; i < N; ++i) {
        const int idx = (ring_pos_ + i) % N;
        fft_real_[i] = input_ring_[idx] * hann_window_[i];
        fft_imag_[i] = 0.0f;
    }

    vivid::simd::fft_forward_scalar(fft_real_.data(), fft_imag_.data(), static_cast<uint32_t>(N));

    for (int i = 0; i < bins; ++i) {
        mag_buf_[i] = std::sqrt(fft_real_[i] * fft_real_[i] + fft_imag_[i] * fft_imag_[i]);
        phase_buf_[i] = std::atan2(fft_imag_[i], fft_real_[i]);
    }

    const bool is_frozen = freeze > 0.5f;
    if (is_frozen && !prev_frozen_) {
        for (int i = 0; i < bins; ++i) {
            frozen_mag_[i] = mag_buf_[i];
            frozen_phase_[i] = phase_buf_[i];
            smoothed_mag_[i] = mag_buf_[i];
        }
        spectrum_captured_ = true;
    }
    prev_frozen_ = is_frozen;

    if (is_frozen && spectrum_captured_) {
        const float alpha = smoothing * 0.99f;
        for (int i = 0; i < bins; ++i)
            smoothed_mag_[i] = smoothed_mag_[i] * alpha + frozen_mag_[i] * (1.0f - alpha);

        for (int i = 0; i < bins; ++i)
            mag_buf_[i] = mag_buf_[i] * (1.0f - blend) + smoothed_mag_[i] * blend;

        if (phase_mode == 1) {
            for (int i = 0; i < bins; ++i)
                phase_buf_[i] = frozen_phase_[i];
        } else if (phase_mode == 2) {
            for (int i = 0; i < bins; ++i)
                phase_buf_[i] = rng_.next() * static_cast<float>(M_PI);
        }
    }

    for (int i = 0; i < bins; ++i) {
        fft_real_[i] = mag_buf_[i] * std::cos(phase_buf_[i]);
        fft_imag_[i] = mag_buf_[i] * std::sin(phase_buf_[i]);
    }

    for (int i = 1; i < N / 2; ++i) {
        fft_real_[N - i] = fft_real_[i];
        fft_imag_[N - i] = -fft_imag_[i];
    }

    vivid::simd::fft_inverse_scalar(fft_real_.data(), fft_imag_.data(), static_cast<uint32_t>(N));

    const float cola_norm = 2.0f / 3.0f;
    for (int i = 0; i < N; ++i) {
        const int out_idx = (ring_pos_ + i) % (2 * N);
        output_accum_[out_idx] += fft_real_[i] * hann_window_[i] * cola_norm;
    }
}

bool Engine::process_fft_frame_accelerate(int N, float freeze, float blend, float smoothing, int phase_mode) {
#if VIVID_ACCELERATE_ENABLED
    if (!fft_cache_.has(static_cast<uint32_t>(N))) return false;
    const int bins = N / 2 + 1;

    for (int i = 0; i < N; ++i) {
        const int idx = (ring_pos_ + i) % N;
        scratch_[i] = input_ring_[idx];
    }
    vDSP_vmul(scratch_.data(), 1, hann_window_.data(), 1, fft_real_.data(), 1, N);
    vDSP_vclr(fft_imag_.data(), 1, N);

    vivid::simd::fft_forward(fft_real_.data(), fft_imag_.data(),
                             static_cast<uint32_t>(N), fft_cache_);

    DSPSplitComplex split{fft_real_.data(), fft_imag_.data()};
    vDSP_zvmags(&split, 1, mag_buf_.data(), 1, bins);
    int count = bins;
    vvsqrtf(mag_buf_.data(), mag_buf_.data(), &count);
    vvatan2f(phase_buf_.data(), fft_imag_.data(), fft_real_.data(), &count);

    const bool is_frozen = freeze > 0.5f;
    if (is_frozen && !prev_frozen_) {
        std::copy(mag_buf_.begin(), mag_buf_.begin() + bins, frozen_mag_.begin());
        std::copy(phase_buf_.begin(), phase_buf_.begin() + bins, frozen_phase_.begin());
        std::copy(mag_buf_.begin(), mag_buf_.begin() + bins, smoothed_mag_.begin());
        spectrum_captured_ = true;
    }
    prev_frozen_ = is_frozen;

    if (is_frozen && spectrum_captured_) {
        const float alpha = smoothing * 0.99f;
        const float one_minus_alpha = 1.0f - alpha;
        vDSP_vsmsma(smoothed_mag_.data(), 1, &alpha, frozen_mag_.data(), 1, &one_minus_alpha,
                    smoothed_mag_.data(), 1, bins);

        const float dry = 1.0f - blend;
        vDSP_vsmsma(mag_buf_.data(), 1, &dry, smoothed_mag_.data(), 1, &blend, mag_buf_.data(), 1, bins);

        if (phase_mode == 1) {
            std::copy(frozen_phase_.begin(), frozen_phase_.begin() + bins, phase_buf_.begin());
        } else if (phase_mode == 2) {
            for (int i = 0; i < bins; ++i)
                phase_buf_[i] = rng_.next() * static_cast<float>(M_PI);
        }
    }

    vvsinf(sin_buf_.data(), phase_buf_.data(), &count);
    vvcosf(cos_buf_.data(), phase_buf_.data(), &count);
    vDSP_vmul(mag_buf_.data(), 1, cos_buf_.data(), 1, fft_real_.data(), 1, bins);
    vDSP_vmul(mag_buf_.data(), 1, sin_buf_.data(), 1, fft_imag_.data(), 1, bins);

    for (int i = 1; i < N / 2; ++i) {
        fft_real_[N - i] = fft_real_[i];
        fft_imag_[N - i] = -fft_imag_[i];
    }

    vivid::simd::fft_inverse(fft_real_.data(), fft_imag_.data(),
                             static_cast<uint32_t>(N), fft_cache_);

    const float cola_norm = 2.0f / 3.0f;
    for (int i = 0; i < N; ++i)
        scratch_[i] = fft_real_[i] * hann_window_[i] * cola_norm;

    for (int i = 0; i < N; ++i) {
        const int out_idx = (ring_pos_ + i) % (2 * N);
        output_accum_[out_idx] += scratch_[i];
    }
    return true;
#else
    (void)N;
    (void)freeze;
    (void)blend;
    (void)smoothing;
    (void)phase_mode;
    return false;
#endif
}

void Engine::process(const float* in,
                     float* out,
                     uint32_t frames,
                     uint32_t sample_rate,
                     int fft_size_param,
                     float freeze,
                     float blend,
                     float smoothing,
                     int phase_mode,
                     Backend backend) {
    if (!in || !out || frames == 0) return;

    lazy_init(sample_rate, fft_size_param);
    const int N = resolve_fft_size(fft_size_param);
    const int hop = N / 4;
    freeze = clamp01(freeze);
    blend = clamp01(blend);
    smoothing = clamp01(smoothing);

    for (uint32_t i = 0; i < frames; ++i) {
        input_ring_[ring_pos_] = in[i];

        const int out_idx = ring_pos_ % (2 * N);
        out[i] = output_accum_[out_idx];
        output_accum_[out_idx] = 0.0f;

        ring_pos_ = (ring_pos_ + 1) % N;
        ++hop_counter_;

        if (hop_counter_ >= hop) {
            hop_counter_ = 0;
            bool used_accelerate = false;
            if (backend == Backend::Accelerate)
                used_accelerate = process_fft_frame_accelerate(N, freeze, blend, smoothing, phase_mode);
            if (!used_accelerate)
                process_fft_frame_scalar(N, freeze, blend, smoothing, phase_mode);
            last_backend_ = used_accelerate ? Backend::Accelerate : Backend::Scalar;
        }
    }
}

} // namespace vivid::spectral_freeze_dsp
