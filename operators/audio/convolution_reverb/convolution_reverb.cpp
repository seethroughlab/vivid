#include "operator_api/operator.h"
#include "shared/convolution_reverb_dsp/convolution_reverb_dsp.h"

/**
 * @brief High-quality stereo convolution reverb.
 *
 * Uses generated built-in impulse responses or a user-provided WAV impulse.
 * The early response is rendered directly for low latency and the tail is
 * rendered by a partitioned FFT engine with an Accelerate backend on macOS.
 *
 * @see Reverb, StereoPanWidth, PingPongDelay
 */
struct ConvolutionReverb : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "ConvolutionReverb";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int> ir_preset{"ir_preset", 2, {"Room", "Plate", "Hall", "Cathedral"}};
    vivid::Param<vivid::FilePath> ir_file{"ir_file"};
    vivid::Param<float> mix{"mix", 0.35f, 0.0f, 1.0f};
    vivid::Param<float> pre_delay_ms{"pre_delay_ms", 0.0f, 0.0f, 250.0f};
    vivid::Param<float> width{"width", 1.0f, 0.0f, 2.0f};
    vivid::Param<float> ir_gain_db{"ir_gain_db", 0.0f, -36.0f, 24.0f};
    vivid::Param<float> tail_seconds{"tail_seconds", 4.0f, 0.05f, 30.0f};

    vivid::convolution_reverb_dsp::Engine engine_;

    ConvolutionReverb() {
        vivid::display_hint(ir_preset, VIVID_DISPLAY_KNOB);
        vivid::description(ir_preset, "Built-in impulse response to use when ir_file is empty");
        vivid::description(ir_file, "Optional WAV impulse response. Mono, stereo, and 4-channel true-stereo files are supported");

        vivid::semantic_tag(mix, "probability_01");
        vivid::semantic_shape(mix, "scalar");
        vivid::semantic_intent(mix, "wet_mix");
        vivid::display_hint(mix, VIVID_DISPLAY_KNOB);
        vivid::description(mix, "Blend between dry input and convolved reverb signal");

        vivid::semantic_unit(pre_delay_ms, "ms");
        vivid::display_hint(pre_delay_ms, VIVID_DISPLAY_KNOB);
        vivid::description(pre_delay_ms, "Delay before the impulse response begins");

        vivid::semantic_tag(width, "amplitude_linear");
        vivid::semantic_shape(width, "scalar");
        vivid::display_hint(width, VIVID_DISPLAY_KNOB);
        vivid::description(width, "Stereo width applied to the wet reverb signal");

        vivid::semantic_unit(ir_gain_db, "dB");
        vivid::display_hint(ir_gain_db, VIVID_DISPLAY_KNOB);
        vivid::description(ir_gain_db, "Gain applied while preparing the impulse response");

        vivid::semantic_unit(tail_seconds, "s");
        vivid::display_hint(tail_seconds, VIVID_DISPLAY_KNOB);
        vivid::description(tail_seconds, "Maximum impulse-response tail length to render");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&ir_preset);
        out.push_back(&ir_file);
        out.push_back(&mix);
        out.push_back(&pre_delay_ms);
        out.push_back(&width);
        out.push_back(&ir_gain_db);
        out.push_back(&tail_seconds);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f});
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f});
        vivid::append_analysis_ports(out);
    }

    void process_audio(const VividAudioContext* ctx) override {
        vivid::convolution_reverb_dsp::ProcessParams params{};
        params.ir_preset = ir_preset.int_value();
        params.ir_file = ir_file.str_value.c_str();
        params.mix = mix.value;
        params.pre_delay_ms = pre_delay_ms.value;
        params.width = width.value;
        params.ir_gain_db = ir_gain_db.value;
        params.tail_seconds = tail_seconds.value;
        engine_.process(ctx->input_buffers[0], ctx->output_buffers[0],
                        ctx->buffer_size, ctx->sample_rate, params);
    }
};

VIVID_REGISTER(ConvolutionReverb)
