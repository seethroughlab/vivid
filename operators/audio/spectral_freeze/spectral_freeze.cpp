#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "operator_api/audio_dsp.h"

#include <cmath>
#include <vector>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// Spectral Freeze — STFT-based spectral snapshot with overlap-add (mono)
// ---------------------------------------------------------------------------

static constexpr int kMaxFFTSize = 1024;

struct SpectralFreeze : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "SpectralFreeze";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> freeze    {"freeze",     0.0f, 0.0f, 1.0f};
    vivid::Param<float> blend     {"blend",      0.0f, 0.0f, 1.0f};
    vivid::Param<float> smoothing {"smoothing",  0.0f, 0.0f, 1.0f};
    vivid::Param<int>   fft_size  {"fft_size",   1, {"256", "512", "1024"}};
    vivid::Param<int>   phase_mode{"phase_mode", 0, {"input", "frozen", "random"}};

    // STFT state
    std::vector<float> input_ring_;
    std::vector<float> output_accum_;
    std::vector<float> hann_window_;
    int ring_pos_     = 0;
    int hop_counter_  = 0;

    // FFT scratch
    std::vector<float> fft_real_;
    std::vector<float> fft_imag_;

    // Polar buffers
    std::vector<float> mag_buf_;
    std::vector<float> phase_buf_;

    // Frozen spectrum
    std::vector<float> frozen_mag_;
    std::vector<float> frozen_phase_;
    std::vector<float> smoothed_mag_;
    bool  spectrum_captured_ = false;
    bool  prev_frozen_       = false;

    audio_dsp::WhiteNoise rng_;
    bool     initialized_ = false;
    uint32_t init_rate_   = 0;
    int      init_fft_    = -1;

    SpectralFreeze() {
        vivid::semantic_tag(freeze, "gate");
        vivid::semantic_shape(freeze, "scalar");
        vivid::display_hint(freeze, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(blend, "probability_01");
        vivid::semantic_shape(blend, "scalar");
        vivid::semantic_intent(blend, "wet_mix");
        vivid::display_hint(blend, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(smoothing, "probability_01");
        vivid::semantic_shape(smoothing, "scalar");
        vivid::display_hint(smoothing, VIVID_DISPLAY_KNOB);

        vivid::display_hint(fft_size, VIVID_DISPLAY_KNOB);
        vivid::display_hint(phase_mode, VIVID_DISPLAY_KNOB);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&freeze);
        out.push_back(&blend);
        out.push_back(&smoothing);
        out.push_back(&fft_size);
        out.push_back(&phase_mode);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",     VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output",    VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"freeze_cv", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"blend_cv",  VIVID_PORT_SIGNAL, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
    }

    int resolve_fft_size() const {
        int idx = fft_size.int_value();
        switch (idx) {
            case 0: return 256;
            case 1: return 512;
            case 2: return 1024;
            default: return 512;
        }
    }

    void lazy_init(uint32_t sr) {
        int N = resolve_fft_size();
        if (initialized_ && init_rate_ == sr && init_fft_ == N) return;

        input_ring_.assign(N, 0.0f);
        output_accum_.assign(2 * N, 0.0f);
        hann_window_.resize(N);
        for (int i = 0; i < N; i++)
            hann_window_[i] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * i / (N - 1)));

        fft_real_.resize(N);
        fft_imag_.resize(N);

        int bins = N / 2 + 1;
        mag_buf_.resize(bins);
        phase_buf_.resize(bins);
        frozen_mag_.assign(bins, 0.0f);
        frozen_phase_.assign(bins, 0.0f);
        smoothed_mag_.assign(bins, 0.0f);

        ring_pos_          = 0;
        hop_counter_       = 0;
        spectrum_captured_ = false;
        prev_frozen_       = false;
        initialized_       = true;
        init_rate_         = sr;
        init_fft_          = N;
    }

    // In-place radix-2 Cooley-Tukey FFT (from fft_analysis.cpp)
    void fft_forward(int N) {
        uint32_t log2N = 0;
        for (uint32_t tmp = N; tmp > 1; tmp >>= 1) ++log2N;

        // Bit-reversal permutation
        for (uint32_t i = 0; i < static_cast<uint32_t>(N); ++i) {
            uint32_t j = 0;
            for (uint32_t b = 0; b < log2N; ++b)
                j |= ((i >> b) & 1) << (log2N - 1 - b);
            if (j > i) {
                std::swap(fft_real_[i], fft_real_[j]);
                std::swap(fft_imag_[i], fft_imag_[j]);
            }
        }

        // Butterfly stages
        for (uint32_t s = 1; s <= log2N; ++s) {
            uint32_t m = 1u << s;
            float wm_re = std::cos(-2.0f * static_cast<float>(M_PI) / m);
            float wm_im = std::sin(-2.0f * static_cast<float>(M_PI) / m);
            for (uint32_t k = 0; k < static_cast<uint32_t>(N); k += m) {
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
    }

    // IFFT: conjugate, forward FFT, conjugate, divide by N
    void fft_inverse(int N) {
        for (int i = 0; i < N; i++)
            fft_imag_[i] = -fft_imag_[i];

        fft_forward(N);

        float inv_N = 1.0f / static_cast<float>(N);
        for (int i = 0; i < N; i++) {
            fft_real_[i] *= inv_N;
            fft_imag_[i] = -fft_imag_[i] * inv_N;
        }
    }

    void process_fft_frame(int N, float freeze_val, float blend_val,
                           float smooth_val, int phase_mode_val) {
        int bins = N / 2 + 1;

        // Extract N samples from ring buffer, apply analysis window
        for (int i = 0; i < N; i++) {
            int idx = (ring_pos_ + i) % N;
            fft_real_[i] = input_ring_[idx] * hann_window_[i];
            fft_imag_[i] = 0.0f;
        }

        // Forward FFT
        fft_forward(N);

        // Convert to polar
        for (int i = 0; i < bins; i++) {
            mag_buf_[i]   = std::sqrt(fft_real_[i] * fft_real_[i] + fft_imag_[i] * fft_imag_[i]);
            phase_buf_[i] = std::atan2(fft_imag_[i], fft_real_[i]);
        }

        bool is_frozen = freeze_val > 0.5f;

        // Rising edge: capture snapshot
        if (is_frozen && !prev_frozen_) {
            for (int i = 0; i < bins; i++) {
                frozen_mag_[i]   = mag_buf_[i];
                frozen_phase_[i] = phase_buf_[i];
                smoothed_mag_[i] = mag_buf_[i];
            }
            spectrum_captured_ = true;
        }
        prev_frozen_ = is_frozen;

        // Apply freeze + blend
        if (is_frozen && spectrum_captured_) {
            // Smooth frozen magnitudes
            float alpha = smooth_val * 0.99f; // smoothing coefficient
            for (int i = 0; i < bins; i++) {
                smoothed_mag_[i] = smoothed_mag_[i] * alpha + frozen_mag_[i] * (1.0f - alpha);
            }

            // Blend live with frozen
            for (int i = 0; i < bins; i++) {
                mag_buf_[i] = mag_buf_[i] * (1.0f - blend_val) + smoothed_mag_[i] * blend_val;
            }

            // Phase mode
            if (phase_mode_val == 1) {
                // Frozen phase
                for (int i = 0; i < bins; i++)
                    phase_buf_[i] = frozen_phase_[i];
            } else if (phase_mode_val == 2) {
                // Random phase
                for (int i = 0; i < bins; i++)
                    phase_buf_[i] = rng_.next() * static_cast<float>(M_PI);
            }
            // phase_mode_val == 0: keep live phase (input)
        }

        // Convert back to rectangular
        for (int i = 0; i < bins; i++) {
            fft_real_[i] = mag_buf_[i] * std::cos(phase_buf_[i]);
            fft_imag_[i] = mag_buf_[i] * std::sin(phase_buf_[i]);
        }

        // Mirror conjugate for bins N/2+1..N-1
        for (int i = 1; i < N / 2; i++) {
            fft_real_[N - i] =  fft_real_[i];
            fft_imag_[N - i] = -fft_imag_[i];
        }

        // IFFT
        fft_inverse(N);

        // Apply synthesis window and overlap-add with COLA normalization (* 2/3)
        int hop = N / 4;
        float cola_norm = 2.0f / 3.0f;
        for (int i = 0; i < N; i++) {
            int out_idx = (ring_pos_ + i) % (2 * N);
            output_accum_[out_idx] += fft_real_[i] * hann_window_[i] * cola_norm;
        }
    }

    void process_audio(const VividAudioContext* ctx) override {
        lazy_init(ctx->sample_rate);

        float* in  = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];
        uint32_t frames = ctx->buffer_size;

        float freeze_cv = ctx->input_float_values ? ctx->input_float_values[0] : 0.0f;
        float blend_cv  = ctx->input_float_values ? ctx->input_float_values[1] : 0.0f;

        float mod_freeze = std::fmax(0.0f, std::fmin(1.0f, freeze.value + freeze_cv));
        float mod_blend  = std::fmax(0.0f, std::fmin(1.0f, blend.value + blend_cv));
        float smooth_val = smoothing.value;
        int   pm         = phase_mode.int_value();

        int N   = resolve_fft_size();
        int hop = N / 4;

        for (uint32_t i = 0; i < frames; i++) {
            // Write input to ring buffer
            input_ring_[ring_pos_] = in[i];

            // Read from output accumulator, then zero the slot
            int out_idx = ring_pos_ % (2 * N);
            out[i] = output_accum_[out_idx];
            output_accum_[out_idx] = 0.0f;

            ring_pos_ = (ring_pos_ + 1) % N;
            hop_counter_++;

            if (hop_counter_ >= hop) {
                hop_counter_ = 0;
                process_fft_frame(N, mod_freeze, mod_blend, smooth_val, pm);
            }
        }
    }
};

VIVID_REGISTER(SpectralFreeze)
