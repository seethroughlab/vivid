// File: include_directives.cpp
// Tests extraction of include targets (both quoted and system).

#include "operator_api/operator.h"
#include "some_local_header.h"
#include "operators/shared/another_header.h"
#include <system_header>
#include <another/system.h>

struct IncludeOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "IncludeOp";
    void collect_params(std::vector<vivid::ParamBase*>&) override {}
    void collect_ports(std::vector<VividPortDescriptor>&) override {}
    void process_frame(const VividFrameContext*) override {}
};

VIVID_REGISTER(IncludeOp)
