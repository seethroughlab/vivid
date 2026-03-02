#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "operator_api/audio_dsp.h"

#include <cmath>
#include <cstring>

// ---------------------------------------------------------------------------
// FreqShift: Bode frequency shifter via Hilbert transform
//
// 31-tap FIR Hilbert transform with Blackman window produces an analytic
// signal (I/Q pair). Multiply by a complex oscillator at the shift frequency
// to move all frequencies up or down. LFO modulates shift amount.
//
// No circular buffer needed — purely algebraic with a short FIR delay line.
// ---------------------------------------------------------------------------

static constexpr int kHilbertTaps = 31;
static constexpr int kHilbertHalf = kHilbertTaps / 2; // 15

struct FreqShift : vivid::OperatorBase {
    static constexpr const char* kName   = "FreqShift";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> phase    {"phase",     0.0f,    0.0f,    1.0f};
    vivid::Param<float> shift    {"shift",     20.0f, -500.0f, 500.0f};
    vivid::Param<float> mod_depth{"mod_depth",  0.0f,   0.0f, 200.0f};
    vivid::Param<float> mod_rate {"mod_rate",   2.0f,   0.1f,  20.0f};
    vivid::Param<float> mix      {"mix",        1.0f,   0.0f,   1.0f};

    // Hilbert FIR coefficients (computed once)
    float hilbert_coeff_[kHilbertTaps] = {};
    bool  coeffs_ready_ = false;

    // Delay line for FIR
    float delay_line_[kHilbertTaps] = {};
    int   delay_idx_ = 0;

    // Oscillator phase
    double osc_phase_ = 0.0;

    // LFO phase
    double lfo_phase_ = 0.0;

    void compute_coefficients() {
        if (coeffs_ready_) return;
        coeffs_ready_ = true;

        // Hilbert transform FIR: h[n] = 2/(pi*n) for odd n, 0 for even n
        // with Blackman window
        for (int i = 0; i < kHilbertTaps; i++) {
            int n = i - kHilbertHalf;
            if (n == 0 || (n % 2) == 0) {
                hilbert_coeff_[i] = 0.0f;
            } else {
                hilbert_coeff_[i] = 2.0f / (static_cast<float>(M_PI) * n);
            }
            // Blackman window
            float w = 0.42f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * i / (kHilbertTaps - 1))
                            + 0.08f * std::cos(4.0f * static_cast<float>(M_PI) * i / (kHilbertTaps - 1));
            hilbert_coeff_[i] *= w;
        }
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase);
        out.push_back(&shift);
        out.push_back(&mod_depth);
        out.push_back(&mod_rate);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        auto* audio = vivid_audio(ctx);
        if (!audio) return;

        compute_coefficients();

        float* in  = audio->input_buffers[0];
        float* out = audio->output_buffers[0];
        uint32_t frames = audio->buffer_size;
        double sr = audio->sample_rate;
        double inv_sr = 1.0 / sr;

        float base_shift = shift.value;
        float depth = mod_depth.value;
        float rate  = mod_rate.value;
        float wet   = mix.value;
        float dry   = 1.0f - wet;

        for (uint32_t i = 0; i < frames; i++) {
            // Write input to delay line
            delay_line_[delay_idx_] = in[i];

            // Compute Hilbert transform (Q component)
            float q_signal = 0.0f;
            for (int t = 0; t < kHilbertTaps; t++) {
                int idx = delay_idx_ - t;
                if (idx < 0) idx += kHilbertTaps;
                q_signal += delay_line_[idx] * hilbert_coeff_[t];
            }

            // I component is the delayed input (center of FIR = kHilbertHalf delay)
            int i_idx = delay_idx_ - kHilbertHalf;
            if (i_idx < 0) i_idx += kHilbertTaps;
            float i_signal = delay_line_[i_idx];

            // LFO modulation
            float lfo = static_cast<float>(std::sin(lfo_phase_ * 2.0 * M_PI));
            float effective_shift = base_shift + lfo * depth;

            // Complex oscillator
            float cos_osc = static_cast<float>(std::cos(osc_phase_ * 2.0 * M_PI));
            float sin_osc = static_cast<float>(std::sin(osc_phase_ * 2.0 * M_PI));

            // Frequency shifting: Re{(I + jQ) * (cos + j*sin)} = I*cos - Q*sin
            float shifted = i_signal * cos_osc - q_signal * sin_osc;

            out[i] = in[i] * dry + shifted * wet;

            // Advance oscillator
            osc_phase_ += effective_shift * inv_sr;
            osc_phase_ -= std::floor(osc_phase_);

            // Advance LFO
            lfo_phase_ += rate * inv_sr;
            lfo_phase_ -= std::floor(lfo_phase_);

            // Advance delay line
            if (++delay_idx_ >= kHilbertTaps) delay_idx_ = 0;
        }
    }
};

VIVID_REGISTER(FreqShift)
