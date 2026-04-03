#include "operator_api/operator.h"

struct PrepareAssetsTestOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "PrepareAssetsTestOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> scale{"scale", 1.0f, 0.0f, 100.0f};
    vivid::Param<vivid::FilePath> path{"path"};

    float prepared_value_ = 0.0f;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&scale);
        out.push_back(&path);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void prepare_instance_assets() override {
        prepared_value_ = scale.value + (path.str_value.empty() ? 0.0f : 1000.0f);
    }

    void process_frame(const VividFrameContext* ctx) override {
        if (ctx && ctx->output_values) ctx->output_values[0] = prepared_value_;
    }
};

VIVID_REGISTER(PrepareAssetsTestOp)
