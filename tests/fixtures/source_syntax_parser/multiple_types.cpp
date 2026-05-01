// File: multiple_types.cpp
// Tests that all type definitions in a single file are found.

#include "operator_api/operator.h"

struct FirstOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "FirstOp";
    void collect_params(std::vector<vivid::ParamBase*>&) override {}
    void collect_ports(std::vector<VividPortDescriptor>&) override {}
    void process_frame(const VividFrameContext*) override {}
};

/**
 * @brief Second operator with docs.
 *
 * This operator has a doc block that should be detected.
 * @tip Second operator tip
 */
struct SecondOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "SecondOp";
    void collect_params(std::vector<vivid::ParamBase*>&) override {}
    void collect_ports(std::vector<VividPortDescriptor>&) override {}
    void process_gpu(const VividGpuContext*) override {}
};

VIVID_REGISTER(FirstOp)
VIVID_REGISTER(SecondOp)
