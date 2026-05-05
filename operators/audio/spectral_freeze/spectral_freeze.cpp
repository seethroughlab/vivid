#include "operator_api/operator.h"
#include "shared/spectral_freeze_dsp/spectral_freeze_dsp.h"

/**
 * @brief STFT-based spectral freeze capturing and sustaining a moment in time.
 *
 * Performs real-time FFT analysis and can freeze the spectral magnitudes
 * on a rising edge of the freeze input. The frozen spectrum sustains
 * indefinitely, blendable with live input. Phase modes control the
 * character of the frozen sound.
 *
 * @param freeze Gate-like input -- rising edge captures the current spectrum.
 * @param phase_mode How phase is handled: input (natural), frozen (static), or random (shimmering).
 * @see GranularSynth, Vocoder
 */
struct SpectralFreeze : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "SpectralFreeze";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> freeze{"freeze", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> blend{"blend", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> smoothing{"smoothing", 0.0f, 0.0f, 1.0f};
    vivid::Param<int> fft_size{"fft_size", 1, {"256", "512", "1024"}};
    vivid::Param<int> phase_mode{"phase_mode", 0, {"input", "frozen", "random"}};

    vivid::spectral_freeze_dsp::Engine engine_;

    SpectralFreeze() {
        vivid::semantic_tag(freeze, "gate");
        vivid::semantic_shape(freeze, "scalar");
        vivid::display_hint(freeze, VIVID_DISPLAY_KNOB);
        vivid::description(freeze, "Rising edge captures the current spectrum");

        vivid::semantic_tag(blend, "probability_01");
        vivid::semantic_shape(blend, "scalar");
        vivid::semantic_intent(blend, "wet_mix");
        vivid::display_hint(blend, VIVID_DISPLAY_KNOB);
        vivid::description(blend, "Mix between live input and frozen spectrum");

        vivid::semantic_tag(smoothing, "probability_01");
        vivid::semantic_shape(smoothing, "scalar");
        vivid::display_hint(smoothing, VIVID_DISPLAY_KNOB);
        vivid::description(smoothing, "Smoothing applied to the frozen magnitude spectrum");

        vivid::display_hint(fft_size, VIVID_DISPLAY_KNOB);
        vivid::description(fft_size, "FFT window size: 256, 512, or 1024 samples");
        vivid::display_hint(phase_mode, VIVID_DISPLAY_KNOB);
        vivid::description(phase_mode, "Phase handling: input (natural), frozen (static), or random (shimmering)");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&freeze);
        out.push_back(&blend);
        out.push_back(&smoothing);
        out.push_back(&fft_size);
        out.push_back(&phase_mode);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT,
                       VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT,
                       VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"freeze_cv", VIVID_PORT_SCALAR, VIVID_PORT_INPUT,
                       VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"blend_cv", VIVID_PORT_SCALAR, VIVID_PORT_INPUT,
                       VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        vivid::append_analysis_ports(out);
    }

    void process_audio(const VividAudioContext* ctx) override {
        const float* in = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];

        const float freeze_cv = ctx->input_buffers[1] ? ctx->input_buffers[1][0] : 0.0f;
        const float blend_cv = ctx->input_buffers[2] ? ctx->input_buffers[2][0] : 0.0f;

        engine_.process(in,
                        out,
                        ctx->buffer_size,
                        ctx->sample_rate,
                        fft_size.int_value(),
                        freeze.value + freeze_cv,
                        blend.value + blend_cv,
                        smoothing.value,
                        phase_mode.int_value());
    }
};

VIVID_DEFINE_OP(SpectralFreeze) {
}

