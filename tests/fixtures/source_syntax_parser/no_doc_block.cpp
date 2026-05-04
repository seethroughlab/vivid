// File: no_doc_block.cpp
// Tests that operators without doc blocks are handled correctly
// (no crash, no spurious doc extraction).

#include "operator_api/operator.h"

struct NoDocOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "NoDocOp";
    void collect_params(std::vector<vivid::ParamBase*>&) override {}
    void collect_ports(std::vector<VividPortDescriptor>&) override {}
    void process_frame(const VividFrameContext*) override {}
};

VIVID_REGISTER(NoDocOp)
