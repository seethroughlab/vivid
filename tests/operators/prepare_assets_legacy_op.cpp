#include "operator_api/operator.h"

#include <vector>

struct PrepareAssetsLegacyOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "PrepareAssetsLegacyOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> scale{"scale", 2.0f, 0.0f, 100.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&scale);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        if (ctx && ctx->output_values) ctx->output_values[0] = scale.value;
    }
};

extern "C" uint32_t vivid_abi_version() {
    return VIVID_OPERATOR_ABI_VERSION;
}

extern "C" const VividOperatorDescriptor* vivid_descriptor() {
    static std::vector<VividParamDescriptor> params;
    static std::vector<VividPortDescriptor> ports;
    static VividOperatorDescriptor desc{};
    static bool inited = false;
    if (!inited) {
        PrepareAssetsLegacyOp op;
        std::vector<vivid::ParamBase*> pbases;
        op.collect_params(pbases);
        params.resize(pbases.size());
        for (size_t i = 0; i < pbases.size(); ++i) {
            params[i].name = pbases[i]->name;
            params[i].type = pbases[i]->type;
            params[i].default_value = pbases[i]->default_value;
            params[i].min_value = pbases[i]->min_value;
            params[i].max_value = pbases[i]->max_value;
            params[i].default_string = nullptr;
            params[i].group = pbases[i]->group;
            params[i].display_hint = pbases[i]->display_hint;
            params[i].layout_columns = pbases[i]->layout_columns;
            params[i].layout_column_index = pbases[i]->layout_column_index;
            params[i].choice_labels = nullptr;
            params[i].choice_count = 0;
            params[i].semantic_tag = pbases[i]->semantic_tag;
            params[i].semantic_shape = pbases[i]->semantic_shape;
            params[i].semantic_unit = pbases[i]->semantic_unit;
            params[i].semantic_intent = pbases[i]->semantic_intent;
            params[i].description = pbases[i]->description;
        }
        op.collect_ports(ports);
        desc.name = PrepareAssetsLegacyOp::kName;
        desc.has_process_audio = 0;
        desc.has_process_gpu = 0;
        desc.has_process_frame = 1;
        desc.multiplicity_behavior = VIVID_MULTIPLICITY_MAP;
        desc.strategy_independent = 0;
        desc.param_count = static_cast<uint32_t>(params.size());
        desc.params = params.data();
        desc.port_count = static_cast<uint32_t>(ports.size());
        desc.ports = ports.data();
        desc.time_dependent = 0;
        inited = true;
    }
    return &desc;
}

extern "C" void* vivid_create() {
    return new PrepareAssetsLegacyOp();
}

extern "C" void vivid_destroy(void* instance) {
    delete static_cast<PrepareAssetsLegacyOp*>(instance);
}

extern "C" void vivid_process_frame(void* instance, VividFrameContext* ctx) {
    static_cast<PrepareAssetsLegacyOp*>(instance)->process_frame(ctx);
}
