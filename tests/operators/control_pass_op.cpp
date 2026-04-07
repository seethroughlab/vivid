// Control passthrough with lane support: output = input * gain
#include "operator_api/operator.h"

struct ControlPassOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "ControlPassOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> gain{"gain", 1.0f, 0.0f, 100.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&gain);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"in",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        float g = ctx->param_values[0];
        float in = ctx->input_values[0];
        ctx->output_values[0] = in * g;

        // Lane propagation: multiply each lane element by gain
        if (ctx->input_lanes && ctx->output_lanes) {
            const auto& isp = ctx->input_lanes[0];
            auto& osp = ctx->output_lanes[0];
            if (isp.length > 0) {
                float* buf = osp.resize(osp.handle, isp.length);
                if (buf) {
                    for (uint32_t i = 0; i < isp.length; ++i) {
                        buf[i] = isp.data[i] * g;
                    }
                    osp.commit(osp.handle, isp.length);
                }
            }
        }
    }
};

VIVID_REGISTER(ControlPassOp)
