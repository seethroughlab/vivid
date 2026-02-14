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
    std::string path;      // e.g. "output.meanBrightness"
    std::string op;        // ">", ">=", "<", "<=", "==", "!="
    double value = 0.0;    // comparison target (for numeric)
    std::string strValue;  // comparison target (for string == / !=)
    std::string message;   // optional failure description
};

struct AssertionResult {
    bool passed = false;
    std::string path;
    std::string op;
    double expected = 0.0;
    double actual = 0.0;
    std::string message;
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
