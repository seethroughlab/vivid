// File: doc_comment_variants.cpp
// Tests detection of doc comments vs. regular comments.

#include "operator_api/operator.h"

/**
 * @brief Block comment with docs.
 * @param x X parameter
 */
struct BlockDocOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "BlockDocOp";
    vivid::Param<float> x {"x", 1.0f, 0.0f, 1.0f};
    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&x);
    }
    void collect_ports(std::vector<VividPortDescriptor>&) override {}
    void process_frame(const VividFrameContext*) override {}
};

// Single-line comment (should NOT be a doc comment)
struct SingleLineCommentOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "SingleLineCommentOp";
    void collect_params(std::vector<vivid::ParamBase*>&) override {}
    void collect_ports(std::vector<VividPortDescriptor>&) override {}
    void process_frame(const VividFrameContext*) override {}
};

/* Block comment that is NOT a doc comment (no @brief, starts with /* not /**) */
struct NonDocBlockOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "NonDocBlockOp";
    void collect_params(std::vector<vivid::ParamBase*>&) override {}
    void collect_ports(std::vector<VividPortDescriptor>&) override {}
    void process_frame(const VividFrameContext*) override {}
};

VIVID_REGISTER(BlockDocOp)
VIVID_REGISTER(SingleLineCommentOp)
VIVID_REGISTER(NonDocBlockOp)
