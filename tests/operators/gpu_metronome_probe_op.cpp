#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/metronome_sync.h"

struct GpuMetronomeProbeOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "GpuMetronomeProbeOp";
    static constexpr bool kTimeDependent = false;

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"enabled", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"bpm", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"beats_per_bar", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"beat_phase", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"bar_phase", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"beats_elapsed", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_gpu(const VividGpuContext* ctx) override {
        const auto metronome = vivid::metronome_transport(ctx);
        ctx->output_values[0] = metronome.enabled ? 1.0f : 0.0f;
        ctx->output_values[1] = metronome.bpm;
        ctx->output_values[2] = static_cast<float>(metronome.beats_per_bar);
        ctx->output_values[3] = metronome.beat_phase;
        ctx->output_values[4] = metronome.bar_phase;
        ctx->output_values[5] = static_cast<float>(metronome.beats_elapsed);
    }
};

VIVID_REGISTER(GpuMetronomeProbeOp)
