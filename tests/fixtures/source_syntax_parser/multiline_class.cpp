// File: multiline_class.cpp
// Tests tree-sitter's ability to handle struct declarations spanning
// multiple lines with different base classes on separate lines.

#include "operator_api/operator.h"

/**
 * @brief Operator with multiline base class declaration.
 *
 * Tests tree-sitter's ability to handle declarations spanning
 * multiple lines with different base classes on separate lines.
 * @tip This is a test fixture for multiline base class parsing.
 */
struct MultilineBaseOp
    : vivid::OperatorBase,
      vivid::GpuProcessable
{
    static constexpr const char* kName = "MultilineBaseOp";
    vivid::Param<float> intensity {"intensity", 1.0f, 0.0f, 1.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&intensity);
    }
    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"output", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }
    void process_gpu(const VividGpuContext*) override {}
};
