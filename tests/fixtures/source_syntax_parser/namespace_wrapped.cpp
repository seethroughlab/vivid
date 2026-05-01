// File: namespace_wrapped.cpp
// Tests finding type definitions inside nested namespaces.

#include "operator_api/operator.h"

namespace vivid {
namespace operators {

/**
 * @brief Operator wrapped in nested namespaces.
 *
 * Verifies that tree-sitter can find struct declarations
 * even when they are inside namespace blocks.
 */
struct NamespaceOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "NamespaceOp";
    void collect_params(std::vector<vivid::ParamBase*>&) override {}
    void collect_ports(std::vector<VividPortDescriptor>&) override {}
    void process_frame(const VividFrameContext*) override {}
};

VIVID_REGISTER(NamespaceOp)

} // namespace operators
} // namespace vivid
