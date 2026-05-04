#include "operator_api/operator.h"
/**
 * @brief Converts beat duration to musical note duration.
 *
 * Takes beat_ms input and multiplies by a subdivision factor to produce
 * the duration of a note value (whole, half, quarter, eighth, etc.,
 * including dotted and triplet variants).
 *
 * @see Clock, Delay
 */
struct NoteDuration : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "NoteDuration";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int> subdivision{"subdivision", 2,
        {"1/1", "1/2", "1/4", "1/8", "1/16", "1/32",
         "dotted 1/4", "dotted 1/8", "dotted 1/16",
         "triplet 1/4", "triplet 1/8", "triplet 1/16"}};

    NoteDuration() {
        vivid::description(subdivision, "Musical note value to convert the beat duration into");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&subdivision);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_ms",     VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"duration_ms", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    // ── Shared helper ──────────────────────────────────────────────────

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

    // ── Frame-rate processing (~60 Hz) ──────────────────────────────────

    void process_frame(const VividFrameContext* ctx) override {
        float beat_ms = ctx->input_values[0];
        int idx = static_cast<int>(ctx->param_values[0]);  // subdivision
        ctx->output_values[0] = beat_ms * kFactors[idx];
    }

};

VIVID_DEFINE_OP(NoteDuration) {
    name = "NoteDuration";
    keywords = {"duration", "note", "subdivision", "timing", "musical", "rhythm"};
    summary = "Converts beat duration to a musical note value (whole, half, quarter, eighth, etc.).";
}

VIVID_REGISTER(NoteDuration)
