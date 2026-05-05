#include "operator_api/operator.h"
#include <cmath>
#include <vector>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
/**
 * @brief Radix-2 FFT computing magnitude spectrum from a waveform lane array.
 *
 * Performs a Cooley-Tukey FFT with optional windowing (Hann, Hamming)
 * on an input waveform lane array and outputs the magnitude spectrum.
 *
 * @see AudioAnalysis, TextureAnalysis
 */
struct FFTAnalysis : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "FFTAnalysis";
    static constexpr bool kTimeDependent = false;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_STRUCTURAL;

    vivid::Param<int> window   {"window",   1, {"none", "hann", "hamming"}};
    vivid::Param<int> fft_size {"fft_size", 512, 256, 1024};

    FFTAnalysis() {
        vivid::description(window, "Windowing function applied before the FFT: none, hann, or hamming");
        vivid::description(fft_size, "Number of FFT samples (256, 512, or 1024)");

        vivid::semantic_tag(fft_size, "count");
        vivid::semantic_shape(fft_size, "int");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&window);
        out.push_back(&fft_size);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"waveform", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});
        out.push_back({"spectrum", VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        // Read input lane array
        if (!ctx->input_lanes || ctx->input_lanes[0].length == 0) {
            ctx->output_values[0] = 0.0f;
            return;
        }

        const float* wave_data = ctx->input_lanes[0].data;
        uint32_t wave_len = ctx->input_lanes[0].length;

        // Determine FFT size (must be power of 2: 256, 512, or 1024)
        uint32_t N = static_cast<uint32_t>(fft_size.int_value());
        if (N != 256 && N != 512 && N != 1024) N = 512;

        // Copy input into working buffer, zero-pad if needed
        buf_real_.resize(N, 0.0f);
        buf_imag_.resize(N, 0.0f);
        uint32_t copy_len = std::min(wave_len, N);
        for (uint32_t i = 0; i < copy_len; ++i) buf_real_[i] = wave_data[i];
        for (uint32_t i = copy_len; i < N; ++i) buf_real_[i] = 0.0f;
        std::fill(buf_imag_.begin(), buf_imag_.end(), 0.0f);

        // Apply window
        int win = window.int_value();
        if (win == 1) {  // Hann
            for (uint32_t i = 0; i < N; ++i)
                buf_real_[i] *= 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * i / (N - 1)));
        } else if (win == 2) {  // Hamming
            for (uint32_t i = 0; i < N; ++i)
                buf_real_[i] *= 0.54f - 0.46f * std::cos(2.0f * static_cast<float>(M_PI) * i / (N - 1));
        }

        // In-place radix-2 Cooley-Tukey FFT
        // Bit-reversal permutation
        uint32_t log2N = 0;
        for (uint32_t tmp = N; tmp > 1; tmp >>= 1) ++log2N;

        for (uint32_t i = 0; i < N; ++i) {
            uint32_t j = 0;
            for (uint32_t b = 0; b < log2N; ++b)
                j |= ((i >> b) & 1) << (log2N - 1 - b);
            if (j > i) {
                std::swap(buf_real_[i], buf_real_[j]);
                std::swap(buf_imag_[i], buf_imag_[j]);
            }
        }

        // Butterfly stages
        for (uint32_t s = 1; s <= log2N; ++s) {
            uint32_t m = 1u << s;
            float wm_re = std::cos(-2.0f * static_cast<float>(M_PI) / m);
            float wm_im = std::sin(-2.0f * static_cast<float>(M_PI) / m);
            for (uint32_t k = 0; k < N; k += m) {
                float w_re = 1.0f, w_im = 0.0f;
                for (uint32_t j = 0; j < m / 2; ++j) {
                    uint32_t t_idx = k + j + m / 2;
                    uint32_t u_idx = k + j;
                    float t_re = w_re * buf_real_[t_idx] - w_im * buf_imag_[t_idx];
                    float t_im = w_re * buf_imag_[t_idx] + w_im * buf_real_[t_idx];
                    buf_real_[t_idx] = buf_real_[u_idx] - t_re;
                    buf_imag_[t_idx] = buf_imag_[u_idx] - t_im;
                    buf_real_[u_idx] = buf_real_[u_idx] + t_re;
                    buf_imag_[u_idx] = buf_imag_[u_idx] + t_im;
                    float new_w_re = w_re * wm_re - w_im * wm_im;
                    float new_w_im = w_re * wm_im + w_im * wm_re;
                    w_re = new_w_re;
                    w_im = new_w_im;
                }
            }
        }

        // Output magnitude spectrum (N/2 bins)
        uint32_t num_bins = N / 2;
        if (!ctx->output_lanes) return;
        auto& out = ctx->output_lanes[0];
        float* buf = out.resize(out.handle, num_bins);
        if (!buf) return;
        float inv_N = 2.0f / N;
        for (uint32_t i = 0; i < num_bins; ++i) {
            float mag = std::sqrt(buf_real_[i] * buf_real_[i] + buf_imag_[i] * buf_imag_[i]) * inv_N;
            buf[i] = mag;
        }
        out.commit(out.handle, num_bins);

        // Scalar fallback: DC component
        ctx->output_values[0] = buf[0];
    }

private:
    std::vector<float> buf_real_;
    std::vector<float> buf_imag_;
};

VIVID_DEFINE_OP(FFTAnalysis) {
}

