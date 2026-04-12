#include "operator_api/operator.h"
#include "shared/vocoder_dsp/vocoder_dsp.h"

// ---------------------------------------------------------------------------
// Vocoder — channel vocoder with SVF bandpass filter bank (mono)
// ---------------------------------------------------------------------------

/**
 * @brief Channel vocoder analyzing a modulator to shape a carrier signal.
 *
 * Splits both modulator and carrier through parallel bandpass filter
 * banks (4-32 bands). Envelope followers on the modulator bands control
 * the amplitude of corresponding carrier bands, transferring the spectral shape.
 *
 * @tip Use speech as modulator and a rich synth (saw wave) as carrier for classic robot voice.
 * @input modulator The signal whose spectral envelope is extracted (typically voice).
 * @input carrier The signal being shaped (typically a synth or noise).
 * @see Filter, ParametricEQ, SpectralFreeze
 */
struct Vocoder : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Vocoder";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int>   bands         {"bands",          16, 4, 32};
    vivid::Param<float> envelope_speed{"envelope_speed", 50.0f, 1.0f, 500.0f};
    vivid::Param<float> mix           {"mix",            1.0f, 0.0f, 1.0f};

    vivid::vocoder_dsp::Engine engine_;

    Vocoder() {
        vivid::semantic_tag(bands, "count");
        vivid::semantic_shape(bands, "scalar");
        vivid::display_hint(bands, VIVID_DISPLAY_KNOB);
        vivid::description(bands, "Number of frequency bands in the filter bank (4-32)");

        vivid::semantic_tag(envelope_speed, "time_milliseconds");
        vivid::semantic_shape(envelope_speed, "scalar");
        vivid::semantic_unit(envelope_speed, "ms");
        vivid::display_hint(envelope_speed, VIVID_DISPLAY_KNOB);
        vivid::description(envelope_speed, "Envelope follower response time in milliseconds");

        vivid::semantic_tag(mix, "probability_01");
        vivid::semantic_shape(mix, "scalar");
        vivid::semantic_intent(mix, "wet_mix");
        vivid::display_hint(mix, VIVID_DISPLAY_KNOB);
        vivid::description(mix, "Blend between dry modulator and vocoded signal");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&bands);
        out.push_back(&envelope_speed);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"modulator", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"carrier",   VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output",    VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"speed_cv",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        vivid::append_analysis_ports(out);
    }

    void process_audio(const VividAudioContext* ctx) override {
        const float* mod_in = ctx->input_buffers[0];
        const float* car_in = ctx->input_buffers[1];
        float* out    = ctx->output_buffers[0];
        const float speed_cv = ctx->input_buffers[2] ? ctx->input_buffers[2][0] : 0.0f;

        vivid::vocoder_dsp::ProcessParams params{};
        params.bands = bands.int_value();
        params.envelope_speed_ms = envelope_speed.value + speed_cv;
        params.mix = mix.value;
        engine_.process(mod_in, car_in, out, ctx->buffer_size, ctx->sample_rate, params);
    }
};

VIVID_REGISTER(Vocoder)
