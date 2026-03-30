// AudioAnalysis — Audio operator that computes per-buffer metrics and outputs
// them as VIVID_PORT_SIGNAL control outputs. This is the Audio→Control bridge,
// mirroring TextureAnalysis's role for GPU→Control.
//
// Strategy:
//   - Passthrough: copy input audio → output audio, so it can be inserted
//     into any audio chain without breaking it.
//   - Analysis: compute RMS, peak, spectral centroid, spectral flux, and
//     zero-crossing rate per buffer. Apply exponential smoothing for stability.
//     Output as control floats for downstream visual/control operators.

#include "operator_api/operator.h"
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * @brief Passthrough audio analyzer outputting RMS, peak, and spectral features.
 *
 * Computes RMS, peak, spectral centroid, spectral flux, and zero-crossing
 * rate per buffer with exponential smoothing. Audio passes through unchanged,
 * so it can be inserted into any chain without breaking it.
 *
 * @param smoothing Exponential averaging factor. Higher = more stable, slower response.
 * @see Scopes, TextureAnalysis
 */
struct AudioAnalysis : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "AudioAnalysis";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> smoothing{"smoothing", 0.9f, 0.0f, 0.99f};

    AudioAnalysis() {
        vivid::semantic_shape(smoothing, "scalar");
        vivid::description(smoothing, "Exponential averaging factor for output stability (higher = smoother, slower)");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&smoothing);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output", VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"rms",               VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"peak",              VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"spectral_centroid", VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"spectral_flux",     VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"zero_crossing_rate", VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        vivid::append_analysis_ports(out);
    }

    void process_audio(const VividAudioContext* ctx) override {
        const float* in  = ctx->input_buffers[0];
        float*       out = ctx->output_buffers[0];
        uint32_t     N   = ctx->buffer_size;

        // Passthrough
        std::memcpy(out, in, N * sizeof(float));

        if (N == 0) return;

        float alpha = smoothing.value;
        float inv_n = 1.0f / static_cast<float>(N);

        // --- RMS ---
        float sum_sq = 0.0f;
        for (uint32_t i = 0; i < N; ++i)
            sum_sq += in[i] * in[i];
        float rms_raw = std::sqrt(sum_sq * inv_n);

        // --- Peak ---
        float peak_raw = 0.0f;
        for (uint32_t i = 0; i < N; ++i) {
            float a = std::fabs(in[i]);
            if (a > peak_raw) peak_raw = a;
        }

        // --- Zero-crossing rate ---
        uint32_t crossings = 0;
        for (uint32_t i = 1; i < N; ++i) {
            if ((in[i] >= 0.0f) != (in[i - 1] >= 0.0f))
                ++crossings;
        }
        float zcr_raw = static_cast<float>(crossings) * inv_n;

        // --- Spectral metrics (in-place radix-2 FFT) ---
        // Use power-of-2 FFT size <= buffer size
        uint32_t fft_n = 1;
        while (fft_n * 2 <= N) fft_n *= 2;

        // Resize working buffers if needed
        if (fft_real_.size() != fft_n) {
            fft_real_.resize(fft_n);
            fft_imag_.resize(fft_n);
            prev_magnitudes_.assign(fft_n / 2, 0.0f);
        }

        // Copy input with Hann window
        for (uint32_t i = 0; i < fft_n; ++i) {
            float w = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * i / (fft_n - 1)));
            fft_real_[i] = in[i] * w;
        }
        std::fill(fft_imag_.begin(), fft_imag_.end(), 0.0f);

        // Bit-reversal permutation
        uint32_t log2n = 0;
        for (uint32_t tmp = fft_n; tmp > 1; tmp >>= 1) ++log2n;

        for (uint32_t i = 0; i < fft_n; ++i) {
            uint32_t j = 0;
            for (uint32_t b = 0; b < log2n; ++b)
                j |= ((i >> b) & 1) << (log2n - 1 - b);
            if (j > i) {
                std::swap(fft_real_[i], fft_real_[j]);
                std::swap(fft_imag_[i], fft_imag_[j]);
            }
        }

        // Butterfly stages
        for (uint32_t s = 1; s <= log2n; ++s) {
            uint32_t m = 1u << s;
            float wm_re = std::cos(-2.0f * static_cast<float>(M_PI) / m);
            float wm_im = std::sin(-2.0f * static_cast<float>(M_PI) / m);
            for (uint32_t k = 0; k < fft_n; k += m) {
                float w_re = 1.0f, w_im = 0.0f;
                for (uint32_t j = 0; j < m / 2; ++j) {
                    uint32_t t_idx = k + j + m / 2;
                    uint32_t u_idx = k + j;
                    float t_re = w_re * fft_real_[t_idx] - w_im * fft_imag_[t_idx];
                    float t_im = w_re * fft_imag_[t_idx] + w_im * fft_real_[t_idx];
                    fft_real_[t_idx] = fft_real_[u_idx] - t_re;
                    fft_imag_[t_idx] = fft_imag_[u_idx] - t_im;
                    fft_real_[u_idx] = fft_real_[u_idx] + t_re;
                    fft_imag_[u_idx] = fft_imag_[u_idx] + t_im;
                    float new_w_re = w_re * wm_re - w_im * wm_im;
                    float new_w_im = w_re * wm_im + w_im * wm_re;
                    w_re = new_w_re;
                    w_im = new_w_im;
                }
            }
        }

        // Compute magnitudes and spectral metrics
        uint32_t num_bins = fft_n / 2;
        float inv_fft = 2.0f / fft_n;
        float sample_rate = ctx->sample_rate > 0 ? static_cast<float>(ctx->sample_rate) : 44100.0f;
        float bin_freq = sample_rate / static_cast<float>(fft_n);

        float weighted_freq_sum = 0.0f;
        float mag_sum = 0.0f;
        float flux_sum = 0.0f;

        for (uint32_t i = 0; i < num_bins; ++i) {
            float mag = std::sqrt(fft_real_[i] * fft_real_[i] + fft_imag_[i] * fft_imag_[i]) * inv_fft;
            float freq = static_cast<float>(i) * bin_freq;

            weighted_freq_sum += freq * mag;
            mag_sum += mag;

            // Spectral flux: sum of positive magnitude differences
            float diff = mag - prev_magnitudes_[i];
            if (diff > 0.0f) flux_sum += diff;

            prev_magnitudes_[i] = mag;
        }

        // Spectral centroid normalized to 0-1 range (relative to Nyquist)
        float nyquist = sample_rate * 0.5f;
        float centroid_raw = (mag_sum > 1e-8f) ? (weighted_freq_sum / mag_sum) / nyquist : 0.0f;
        centroid_raw = std::min(1.0f, std::max(0.0f, centroid_raw));

        // Spectral flux normalized (empirical scaling)
        float flux_raw = std::min(1.0f, flux_sum * 4.0f);

        // Apply exponential smoothing
        rms_      = alpha * rms_      + (1.0f - alpha) * rms_raw;
        peak_     = alpha * peak_     + (1.0f - alpha) * peak_raw;
        centroid_ = alpha * centroid_ + (1.0f - alpha) * centroid_raw;
        flux_     = alpha * flux_     + (1.0f - alpha) * flux_raw;
        zcr_      = alpha * zcr_      + (1.0f - alpha) * zcr_raw;

        // Write control float outputs (indices match float output port order)
        ctx->output_float_values[0] = rms_;
        ctx->output_float_values[1] = peak_;
        ctx->output_float_values[2] = centroid_;
        ctx->output_float_values[3] = flux_;
        ctx->output_float_values[4] = zcr_;
    }

private:
    // Smoothed output values
    float rms_      = 0.0f;
    float peak_     = 0.0f;
    float centroid_ = 0.0f;
    float flux_     = 0.0f;
    float zcr_      = 0.0f;

    // FFT working buffers
    std::vector<float> fft_real_;
    std::vector<float> fft_imag_;
    std::vector<float> prev_magnitudes_;
};

VIVID_REGISTER(AudioAnalysis)
