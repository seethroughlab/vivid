// File: malformed_cpp.cpp
// Tests graceful fallback on malformed/incomplete C++ code.
// This file is intentionally broken — tree-sitter should handle it.

#include "operator_api/operator.h"

struct MalformedOp : vivid::OperatorBase {
    static constexpr const char* kName = "MalformedOp";
    void collect_params(std::vector<vivid::ParamBase*>&) override {
        // Missing closing brace intentionally
    void collect_ports(std::vector<VividPortDescriptor>&) override {
