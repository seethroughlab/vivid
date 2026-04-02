#include "operator_api/operator.h"

struct TestOpAbiV4 : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "TestOpAbiV4";
    static constexpr bool kTimeDependent = false;

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = 1.0f;
    }
};

extern "C" const VividOperatorDescriptor* vivid_descriptor() {
    static std::vector<VividPortDescriptor> s_ports;
    static VividOperatorDescriptor s_desc{};
    static bool init = false;
    if (!init) {
        TestOpAbiV4 op;
        op.collect_ports(s_ports);
        s_desc.name = TestOpAbiV4::kName;
        s_desc.param_count = 0;
        s_desc.params = nullptr;
        s_desc.port_count = static_cast<uint32_t>(s_ports.size());
        s_desc.ports = s_ports.data();
        s_desc.time_dependent = TestOpAbiV4::kTimeDependent ? 1 : 0;
        s_desc.has_process_audio = 0;
        s_desc.has_process_gpu = 0;
        s_desc.has_process_frame = 1;
        s_desc.lane_behavior = VIVID_LANE_POINTWISE;
        s_desc.strategy_independent = 1;
        init = true;
    }
    return &s_desc;
}

extern "C" uint32_t vivid_abi_version() {
    return 4u;
}

extern "C" void* vivid_create() {
    return new TestOpAbiV4();
}

extern "C" void vivid_destroy(void* instance) {
    delete static_cast<TestOpAbiV4*>(instance);
}

extern "C" void vivid_process_frame(void* instance, const VividFrameContext* ctx) {
    static_cast<TestOpAbiV4*>(instance)->process_frame(ctx);
}
