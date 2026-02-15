// Vivid Assertion System
// Load and evaluate assertions against chain inspection data
// Used by `vivid check` for autonomous validation

#pragma once

#include <vivid/chain.h>
#include <string>
#include <vector>
#include <utility>

namespace vivid {

struct Assertion {
    std::string name;      // optional human-readable name (e.g. "feedback-alive")
    std::string path;      // e.g. "output.meanBrightness"
    std::string op;        // ">", ">=", "<", "<=", "==", "!=", "between", "exists", "not_exists"
    double value = 0.0;    // comparison target (for numeric)
    double valueHigh = 0.0; // upper bound for "between" operator
    std::string strValue;  // comparison target (for string == / !=)
    std::string message;   // optional failure description

    // Conditional guards
    int afterFrame = -1;       // Skip if current frame < afterFrame
    std::string whenPath;      // Guard condition: dot-path to check
    std::string whenCheck;     // Guard condition: comparison operator
    double whenValue = 0.0;    // Guard condition: expected value
};

struct AssertionResult {
    bool passed = false;
    bool skipped = false;
    std::string name;
    std::string path;
    std::string op;
    double expected = 0.0;
    double expectedHigh = 0.0; // upper bound for "between" operator
    double actual = 0.0;
    std::string message;
    std::string skipReason;
};

struct CheckReport {
    bool allPassed = true;
    int frame = 0;
    std::vector<AssertionResult> results;

    std::string toJSON() const;
    std::string toVerbose() const;
};

// Load assertions from JSON file
// Sets `frame` to the file's "frame" value (or -1 if not specified)
std::vector<Assertion> loadAssertions(const std::string& path, int& frame);

// Evaluate assertions against inspection data
CheckReport evaluateAssertions(const std::vector<Assertion>& assertions,
                               const ChainInspection& inspection, int frame);

// Resolve a dot-path to a float value from ChainInspection
// Returns {true, value} or {false, 0} if path not found
std::pair<bool, double> resolvePath(const std::string& path,
                                    const ChainInspection& inspection);

} // namespace vivid
