// Audio test operator for audio-frame bridge many-value snapshot testing.
// Reads a many-value input, copies it to the many-value output, and emits a
// DC audio signal equal to the sum of the values. Uses the value API
// (ctx->values/value_outputs) — successor to the lane views. (7e.1)
#include "operator_api/operator.h"
#include <cstring>

struct AudioLaneOp : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "AudioLaneOp";
    static constexpr bool kTimeDependent = false;

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({.name="values", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({"out",    VIVID_PORT_AUDIO_BUFFER,    VIVID_PORT_OUTPUT});
        out.push_back({.name="echo", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_OUTPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
        vivid::append_analysis_ports(out);
    }

    void process_audio(const VividAudioContext* ctx) override {
        // Sum the many-value input (port 0 "values")
        float sum = 0.0f;
        const float* in  = ctx->values ? vivid_value_floats(&ctx->values[0]) : nullptr;
        const uint32_t n = ctx->values ? vivid_value_count(&ctx->values[0]) : 0;
        for (uint32_t i = 0; i < n; ++i) sum += in[i];

        // Echo the many-value array to output port 1 ("echo")
        if (ctx->value_outputs && in && n > 0) {
            float* buf = vivid_value_output_floats(&ctx->value_outputs[1], n);
            if (buf) {
                std::memcpy(buf, in, n * sizeof(float));
                vivid_value_output_commit(&ctx->value_outputs[1], n);
            }
        }

        // Write DC audio output = sum (port 0 "out", AUDIO_BUFFER)
        float* out = ctx->output_buffers[0];
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            out[i] = sum;
        }
    }
};

