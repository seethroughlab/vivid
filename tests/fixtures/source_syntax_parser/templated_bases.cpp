// File: templated_bases.cpp
// Tests extraction of base class names from templated base declarations.

#include "operator_api/operator.h"

// Template base classes should extract the base name, not the full template.
struct TemplatedOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "TemplatedOp";
    void collect_params(std::vector<vivid::ParamBase*>&) override {}
    void collect_ports(std::vector<VividPortDescriptor>&) override {}
    void process_gpu(const VividGpuContext*) override {}
};

