#include "operator_api/operator.h"
#include "operator_api/audio_dsp.h"
#include "operator_api/thumbnail.h"
#include "operator_api/draw_plot_helpers.h"
#include <cmath>

/**
 * @brief Basic waveform oscillator with frequency and amplitude CV.
 *
 * Phase-accumulating oscillator generating sine, saw, square, or triangle
 * waveforms. CV inputs accept control signals with +/-120 semitone range.
 *
 * @see FmSynth, LFO, Noise
 */
struct Oscillator : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Oscillator";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> frequency{"frequency", 440.0f, 20.0f, 20000.0f};
    vivid::Param<float> amplitude{"amplitude", 0.5f, 0.0f, 1.0f};
    vivid::Param<int>   waveform {"waveform",  0, {"sine", "saw", "square", "triangle"}};

    double phase_ = 0.0;
    ~Oscillator() override = default;

    Oscillator() {
        vivid::semantic_tag(frequency, "frequency_hz");
        vivid::semantic_shape(frequency, "scalar");
        vivid::semantic_unit(frequency, "Hz");
        vivid::description(frequency, "Base pitch of the oscillator in Hz");

        vivid::semantic_tag(amplitude, "amplitude_linear");
        vivid::semantic_shape(amplitude, "scalar");
        vivid::description(amplitude, "Output level of the waveform");
        vivid::description(waveform, "Shape of the generated waveform");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&frequency);
        out.push_back(&amplitude);
        out.push_back(&waveform);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"output",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 0, 0.0f});
        out.push_back({"freq_cv", VIVID_PORT_SCALAR, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"amp_cv",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 1.0f});
        vivid::append_analysis_ports(out);
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx || !ctx->draw.opaque) return;
        auto& d = const_cast<VividDrawAPI&>(ctx->draw);
        void* o = d.opaque;

        float w = static_cast<float>(ctx->thumbnail_logical_width ? ctx->thumbnail_logical_width : ctx->thumbnail_width);
        float h = static_cast<float>(ctx->thumbnail_logical_height ? ctx->thumbnail_logical_height : ctx->thumbnail_height);
        int wave = (ctx->param_count > 2) ? static_cast<int>(ctx->param_values[2]) : 0;
        float amp = (ctx->param_count > 1) ? std::clamp(ctx->param_values[1], 0.0f, 1.0f) : 0.5f;

        vivid::draw_plot::draw_thumb_background(d, o, w, h);

        const char* wave_name = "SIN";
        switch (wave) {
            case 1: wave_name = "SAW"; break;
            case 2: wave_name = "SQR"; break;
            case 3: wave_name = "TRI"; break;
            default: break;
        }
        vivid::draw_plot::draw_thumb_label(d, o, 6.0f, 4.0f, wave_name, {0.45f, 0.55f, 0.65f, 0.9f}, 0.8f);

        auto sample_fn = [wave, amp](float phase) {
            float p = phase - std::floor(phase);
            float raw = 0.0f;
            switch (wave) {
                case 0: raw = std::sin(p * 2.0f * static_cast<float>(M_PI)); break;
                case 1: raw = 2.0f * p - 1.0f; break;
                case 2: raw = (p < 0.5f) ? 1.0f : -1.0f; break;
                case 3: raw = 4.0f * ((p < 0.5f) ? p : 1.0f - p) - 1.0f; break;
                default: raw = std::sin(p * 2.0f * static_cast<float>(M_PI)); break;
            }
            return raw * amp;
        };

        vivid::draw_plot::draw_waveform_plot(d, o,
                                             8.0f, 20.0f, w - 16.0f, h - 26.0f,
                                             sample_fn,
                                             {0.31f, 0.51f, 0.75f, 0.35f},
                                             {0.63f, 0.78f, 0.94f, 0.95f},
                                             {0.24f, 0.25f, 0.29f, 0.7f},
                                             true,
                                             2.0f,
                                             2.0f);
    }

    void process_audio(const VividAudioContext* ctx) override {
        float* out = ctx->output_buffers[0];
        float freq_cv_val = ctx->input_buffers[0] ? ctx->input_buffers[0][0] : 0.0f;
        float amp_cv_val  = ctx->input_buffers[1] ? ctx->input_buffers[1][0] : 1.0f;
        // Clamp CV to ±120 semitones (~10 octaves) to prevent pow() overflow.
        if (freq_cv_val < -120.0f) freq_cv_val = -120.0f;
        if (freq_cv_val >  120.0f) freq_cv_val =  120.0f;
        float mod_freq = frequency.value * std::pow(2.0f, freq_cv_val / 12.0f);
        float mod_amp  = amplitude.value * amp_cv_val;
        double phase_inc = static_cast<double>(mod_freq) / ctx->sample_rate;
        // Recover from NaN/Inf in phase accumulator (defensive).
        if (!std::isfinite(phase_)) phase_ = 0.0;
        int wave = waveform.int_value();

        for (uint32_t i = 0; i < ctx->buffer_size; i++) {
            double sample = audio_dsp::waveform(phase_, wave);
            out[i] = static_cast<float>(sample) * mod_amp;
            phase_ += phase_inc;
            if (phase_ >= 1.0) phase_ -= 1.0;
        }
    }
};

VIVID_DEFINE_OP(Oscillator) {
}

VIVID_THUMBNAIL(Oscillator)
