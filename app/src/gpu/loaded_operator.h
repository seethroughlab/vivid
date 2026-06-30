#pragma once

#include "operator_api/operator.h"
#include "gpu/operator_loader.h"
#include <vector>

// Adapts a dlopen'd operator (an opaque void* instance + extern "C" fn-ptrs +
// a C descriptor) to the in-process OperatorBase/GpuProcessable interface, so a
// loaded operator flows through OpRegistry::create() → build_descriptor() →
// VisualGraph::render() identically to a built-in. The dylib's params are mirrored
// into synthetic ParamBase objects (build_descriptor reads only plain ParamBase
// fields, so the mirror is lossless); the dylib re-syncs its own params from
// ctx->param_values inside vivid_process_gpu, so the synthetic params exist purely
// to satisfy descriptor build + the resolved-value index contract.
namespace vivid {

class LoadedOperator : public OperatorBase, public GpuProcessable {
public:
    // `loader` is non-owning and must outlive this operator (App owns the loaders).
    explicit LoadedOperator(const OperatorLoader* loader);
    ~LoadedOperator() override;

    void collect_params(std::vector<ParamBase*>& out) override;
    void collect_ports(std::vector<VividPortDescriptor>& out) override;
    void process_gpu(const VividGpuContext* ctx) override;

private:
    const OperatorLoader*            loader_   = nullptr;
    void*                            instance_ = nullptr;
    std::vector<ParamBase>           synth_params_;       // mirrored from the dylib descriptor
    std::vector<ParamBase*>          synth_param_ptrs_;   // collect_params order
    std::vector<VividPortDescriptor> ports_;              // copied from the dylib descriptor
};

}  // namespace vivid
