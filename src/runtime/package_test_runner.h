#pragma once

#include <string>
#include <vector>

namespace vivid {

class PackageManager;
class PackageCompiler;
class OperatorRegistry;

struct SingleTestResult {
    std::string name;    // "tests/basic.json" or "tests/test_ops.cpp"
    std::string type;    // "graph" or "cpp"
    std::string status;  // "passed", "failed", "skipped"
    std::string reason;  // empty on pass; reason for skip/fail
    std::string output;  // captured output for cpp tests
};

struct PackageTestResult {
    bool success = false;
    std::string error;
    std::string package_name;
    int total = 0, passed = 0, failed = 0, skipped = 0;
    std::vector<SingleTestResult> tests;
};

PackageTestResult run_package_tests(const std::string& name,
                                     PackageManager& pm,
                                     PackageCompiler& compiler,
                                     OperatorRegistry& registry);

} // namespace vivid
