// Vivid Assertion System — Implementation
// JSON parsing, dot-path resolution, and assertion evaluation

#include <vivid/assertion.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

using json = nlohmann::json;

namespace vivid {

// Split a string by delimiter
static std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> parts;
    std::istringstream stream(s);
    std::string part;
    while (std::getline(stream, part, delim)) {
        parts.push_back(part);
    }
    return parts;
}

std::vector<Assertion> loadAssertions(const std::string& path, int& frame) {
    std::vector<Assertion> assertions;
    frame = -1;

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open assertion file: " << path << std::endl;
        return assertions;
    }

    json j;
    try {
        j = json::parse(file);
    } catch (const json::parse_error& e) {
        std::cerr << "Error: Invalid JSON in assertion file: " << e.what() << std::endl;
        return assertions;
    }

    if (j.contains("frame") && j["frame"].is_number()) {
        frame = j["frame"].get<int>();
    }

    if (!j.contains("assertions") || !j["assertions"].is_array()) {
        std::cerr << "Error: Assertion file missing 'assertions' array" << std::endl;
        return assertions;
    }

    for (const auto& item : j["assertions"]) {
        Assertion a;
        a.path = item.value("path", "");
        a.op = item.value("op", "");
        a.message = item.value("message", "");

        if (a.path.empty() || a.op.empty()) {
            std::cerr << "Warning: Skipping assertion with missing path or op" << std::endl;
            continue;
        }

        if (item.contains("value")) {
            if (item["value"].is_number()) {
                a.value = item["value"].get<double>();
            } else if (item["value"].is_string()) {
                a.strValue = item["value"].get<std::string>();
            }
        }

        assertions.push_back(std::move(a));
    }

    return assertions;
}

std::pair<bool, double> resolvePath(const std::string& path,
                                    const ChainInspection& inspection) {
    auto parts = split(path, '.');
    if (parts.empty()) return {false, 0.0};

    // output.<field> -> FrameAnalysis
    if (parts[0] == "output" && parts.size() >= 2) {
        const auto& fa = inspection.outputAnalysis;

        if (parts[1] == "meanBrightness") return {true, fa.meanBrightness};
        if (parts[1] == "contrast") return {true, fa.contrast};
        if (parts[1] == "dominantHue") return {true, fa.dominantHue};
        if (parts[1] == "saturationAvg") return {true, fa.saturationAvg};

        // dominantColor.0, dominantColor.1, dominantColor.2
        if (parts[1] == "dominantColor" && parts.size() >= 3) {
            int idx = std::stoi(parts[2]);
            if (idx >= 0 && idx < 3) return {true, fa.dominantColor[idx]};
        }

        // regionBrightness.0 through regionBrightness.8
        if (parts[1] == "regionBrightness" && parts.size() >= 3) {
            int idx = std::stoi(parts[2]);
            if (idx >= 0 && idx < 9) return {true, fa.regionBrightness[idx]};
        }

        // histogram.0 through histogram.7
        if (parts[1] == "histogram" && parts.size() >= 3) {
            int idx = std::stoi(parts[2]);
            if (idx >= 0 && idx < 8) return {true, static_cast<double>(fa.histogram[idx])};
        }

        return {false, 0.0};
    }

    // audio.<field> -> AudioAnalysis
    if (parts[0] == "audio" && parts.size() >= 2) {
        const auto& aa = inspection.audioAnalysis;

        if (parts[1] == "rmsLevel") return {true, aa.rmsLevel};
        if (parts[1] == "peakLevel") return {true, aa.peakLevel};
        if (parts[1] == "rmsLeft") return {true, aa.rmsLeft};
        if (parts[1] == "rmsRight") return {true, aa.rmsRight};
        if (parts[1] == "crestFactor") return {true, aa.crestFactor};
        if (parts[1] == "duration") return {true, aa.duration};
        if (parts[1] == "isSilent") return {true, aa.isSilent ? 1.0 : 0.0};

        // audio.spectrum.<band> -> spectrum array
        if (parts[1] == "spectrum" && parts.size() >= 3) {
            if (parts[2] == "subBass")  return {true, aa.spectrum[0]};
            if (parts[2] == "bass")     return {true, aa.spectrum[1]};
            if (parts[2] == "lowMid")   return {true, aa.spectrum[2]};
            if (parts[2] == "mid")      return {true, aa.spectrum[3]};
            if (parts[2] == "highMid")  return {true, aa.spectrum[4]};
            if (parts[2] == "high")     return {true, aa.spectrum[5]};
        }

        return {false, 0.0};
    }

    // operators.<name>.metrics.<key> -> InspectData float metric
    // operators.<name>.metadata.<key> -> handled separately (string)
    if (parts[0] == "operators" && parts.size() >= 4) {
        const std::string& opName = parts[1];
        const std::string& category = parts[2];
        const std::string& key = parts[3];

        for (const auto& [name, data] : inspection.operators) {
            if (name == opName) {
                if (category == "metrics") {
                    auto it = data.metrics.find(key);
                    if (it != data.metrics.end()) {
                        return {true, static_cast<double>(it->second)};
                    }
                }
                return {false, 0.0};
            }
        }
        return {false, 0.0};
    }

    return {false, 0.0};
}

// Resolve a dot-path to a string value (for metadata comparisons)
static std::pair<bool, std::string> resolveStringPath(const std::string& path,
                                                       const ChainInspection& inspection) {
    auto parts = split(path, '.');
    if (parts[0] == "operators" && parts.size() >= 4) {
        const std::string& opName = parts[1];
        const std::string& category = parts[2];
        const std::string& key = parts[3];

        for (const auto& [name, data] : inspection.operators) {
            if (name == opName && category == "metadata") {
                auto it = data.metadata.find(key);
                if (it != data.metadata.end()) {
                    return {true, it->second};
                }
            }
        }
    }
    return {false, ""};
}

static bool compareValues(double actual, const std::string& op, double expected) {
    if (op == ">")  return actual > expected;
    if (op == ">=") return actual >= expected;
    if (op == "<")  return actual < expected;
    if (op == "<=") return actual <= expected;
    if (op == "==") return actual == expected;
    if (op == "!=") return actual != expected;
    return false;
}

CheckReport evaluateAssertions(const std::vector<Assertion>& assertions,
                               const ChainInspection& inspection, int frame) {
    CheckReport report;
    report.frame = frame;
    report.allPassed = true;

    for (const auto& a : assertions) {
        AssertionResult r;
        r.path = a.path;
        r.op = a.op;
        r.expected = a.value;

        // Check if this is a string comparison (metadata)
        if (!a.strValue.empty() || (a.op == "==" || a.op == "!=")) {
            auto parts = split(a.path, '.');
            if (parts.size() >= 4 && parts[0] == "operators" && parts[2] == "metadata") {
                auto [found, strVal] = resolveStringPath(a.path, inspection);
                if (!found) {
                    r.passed = false;
                    r.message = a.message.empty()
                        ? ("Path not found: " + a.path)
                        : a.message;
                    r.actual = 0.0;
                } else {
                    bool match = (strVal == a.strValue);
                    r.passed = (a.op == "==") ? match : !match;
                    r.message = a.message.empty()
                        ? (a.path + " " + a.op + " \"" + a.strValue + "\" (actual: \"" + strVal + "\")")
                        : a.message;
                    r.actual = r.passed ? 1.0 : 0.0;
                }
                if (!r.passed) report.allPassed = false;
                report.results.push_back(std::move(r));
                continue;
            }
        }

        // Numeric comparison
        auto [found, actual] = resolvePath(a.path, inspection);
        r.actual = actual;

        if (!found) {
            r.passed = false;
            r.message = a.message.empty()
                ? ("Path not found: " + a.path)
                : a.message;
        } else {
            r.passed = compareValues(actual, a.op, a.value);
            if (!r.passed) {
                if (!a.message.empty()) {
                    r.message = a.message;
                } else {
                    std::ostringstream ss;
                    ss << a.path << ": expected " << a.op << " " << a.value
                       << ", got " << actual;
                    r.message = ss.str();
                }
            }
        }

        if (!r.passed) report.allPassed = false;
        report.results.push_back(std::move(r));
    }

    return report;
}

std::string CheckReport::toJSON() const {
    json j;
    j["allPassed"] = allPassed;
    j["frame"] = frame;

    json resultsArr = json::array();
    for (const auto& r : results) {
        json rj;
        rj["passed"] = r.passed;
        rj["path"] = r.path;
        rj["op"] = r.op;
        rj["expected"] = r.expected;
        rj["actual"] = r.actual;
        if (!r.message.empty()) {
            rj["message"] = r.message;
        }
        resultsArr.push_back(rj);
    }
    j["results"] = resultsArr;

    return j.dump(2);
}

std::string CheckReport::toVerbose() const {
    std::ostringstream ss;
    for (const auto& r : results) {
        ss << (r.passed ? "PASS" : "FAIL") << "  " << r.path
           << " " << r.op << " " << r.expected
           << " (actual: " << r.actual << ")";
        if (!r.passed && !r.message.empty()) {
            ss << "  -- " << r.message;
        }
        ss << "\n";
    }

    int passed = 0, failed = 0;
    for (const auto& r : results) {
        if (r.passed) passed++; else failed++;
    }
    ss << "\n" << passed << " passed, " << failed << " failed"
       << " (frame " << frame << ")\n";

    return ss.str();
}

} // namespace vivid
