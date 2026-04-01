#include "clock.h"

struct ClockFr : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "clock_fr";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> bpm{"bpm", 120.0f, 1.0f, 300.0f};
    vivid::Param<int>   beats_per_bar{"beats_per_bar", 4, 1, 16};
    double phase_ = 0.0;
    double bar_phase_ = 0.0;
    double prev_phase_ = 0.0;

    ClockFr() {
        vivid::semantic_tag(bpm, "bpm");
        vivid::semantic_shape(bpm, "scalar");
        vivid::semantic_unit(bpm, "bpm");
        vivid::description(bpm, "Tempo in beats per minute");
        vivid::description(beats_per_bar, "Number of beats in each bar for the bar_trigger output");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&bpm);
        out.push_back(&beats_per_bar);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_phase",   VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
        out.push_back({"beat_ms",      VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
        out.push_back({"bar_phase",    VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
        out.push_back({"beat_trigger", VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        double beats_per_sec = static_cast<double>(bpm.value) / 60.0;
        double bars_per_sec = beats_per_sec / static_cast<double>(beats_per_bar.value);
        prev_phase_ = phase_;
        phase_ += ctx->delta_time * beats_per_sec;
        phase_ -= std::floor(phase_);
        bar_phase_ += ctx->delta_time * bars_per_sec;
        bar_phase_ -= std::floor(bar_phase_);
        ctx->output_values[0] = static_cast<float>(phase_);
        ctx->output_values[1] = 60000.0f / bpm.value;
        ctx->output_values[2] = static_cast<float>(bar_phase_);
        ctx->output_values[3] = (phase_ < prev_phase_) ? 1.0f : 0.0f;
    }
};

VIVID_REGISTER(ClockFr)
