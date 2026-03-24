#include "operator_api/operator.h"

struct NoteDuration : vivid::AudioOperatorBase {
    static constexpr const char* kName   = "NoteDuration";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int> subdivision{"subdivision", 2,
        {"1/1", "1/2", "1/4", "1/8", "1/16", "1/32",
         "dotted 1/4", "dotted 1/8", "dotted 1/16",
         "triplet 1/4", "triplet 1/8", "triplet 1/16"}};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&subdivision);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_ms",     VIVID_PORT_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"duration_ms", VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process_audio(const VividAudioContext* ctx) override {
        static constexpr float kFactors[] = {
            4.0f,    // 1/1
            2.0f,    // 1/2
            1.0f,    // 1/4
            0.5f,    // 1/8
            0.25f,   // 1/16
            0.125f,  // 1/32
            1.5f,    // dotted 1/4
            0.75f,   // dotted 1/8
            0.375f,  // dotted 1/16
            0.667f,  // triplet 1/4
            0.333f,  // triplet 1/8
            0.167f,  // triplet 1/16
        };

        float beat_ms = ctx->input_float_values[0];
        int idx = subdivision.int_value();
        ctx->output_float_values[0] = beat_ms * kFactors[idx];
    }
};

VIVID_REGISTER(NoteDuration)
