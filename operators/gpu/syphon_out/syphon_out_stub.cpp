#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"

/**
 * @brief Sends video frames via Syphon (macOS inter-app video sharing).
 *
 * Publishes the input texture as a Syphon server that other applications
 * can subscribe to for real-time video sharing.
 *
 * @see SyphonIn
 */
struct SyphonOut : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "SyphonOut";
    static constexpr bool kTimeDependent = true;

    vivid::Param<bool> active{"active", false};
    vivid::Param<vivid::TextValue> server_name{"server_name", "Vivid Output"};

    SyphonOut() {
        vivid::description(active, "Enable or disable Syphon frame publishing");
        vivid::description(server_name, "Name other apps will see for this Syphon server");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&active);
        out.push_back(&server_name);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input", VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
    }

    void process_gpu(const VividGpuContext*) override {}
};

VIVID_DEFINE_OP(SyphonOut) {
}

