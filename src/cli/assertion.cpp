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
        a.name = item.value("name", "");
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
            } else if (item["value"].is_array() && item["value"].size() == 2) {
                // Array value for "between" operator: [low, high]
                a.value = item["value"][0].get<double>();
                a.valueHigh = item["value"][1].get<double>();
            }
        }

        // Conditional guards
        if (item.contains("after_frame") && item["after_frame"].is_number()) {
            a.afterFrame = item["after_frame"].get<int>();
        }
        if (item.contains("when_path") && item["when_path"].is_string()) {
            a.whenPath = item["when_path"].get<std::string>();
        }
        if (item.contains("when_check") && item["when_check"].is_string()) {
            a.whenCheck = item["when_check"].get<std::string>();
        }
        if (item.contains("when_value") && item["when_value"].is_number()) {
            a.whenValue = item["when_value"].get<double>();
        }

        assertions.push_back(std::move(a));
    }

    return assertions;
}

// Resolve a FrameAnalysis field by name (shared between output.* and operators.*.textureAnalysis.*)
// `parts` starts at the field name (e.g. "meanBrightness", "histogram", "dominantColor")
// `partIdx` is the index of the field name within the original dot-path parts
static std::pair<bool, double> resolveFrameAnalysisField(const FrameAnalysis& fa,
                                                          const std::vector<std::string>& parts,
                                                          size_t partIdx) {
    if (partIdx >= parts.size()) return {false, 0.0};

    const std::string& field = parts[partIdx];

    if (field == "meanBrightness") return {true, fa.meanBrightness};
    if (field == "contrast") return {true, fa.contrast};
    if (field == "dominantHue") return {true, fa.dominantHue};
    if (field == "saturationAvg") return {true, fa.saturationAvg};

    // dominantColor.0, dominantColor.1, dominantColor.2
    if (field == "dominantColor" && partIdx + 1 < parts.size()) {
        int idx = std::stoi(parts[partIdx + 1]);
        if (idx >= 0 && idx < 3) return {true, fa.dominantColor[idx]};
    }

    // regionBrightness.0 through regionBrightness.8
    if (field == "regionBrightness" && partIdx + 1 < parts.size()) {
        int idx = std::stoi(parts[partIdx + 1]);
        if (idx >= 0 && idx < 9) return {true, fa.regionBrightness[idx]};
    }

    // histogram.0 through histogram.7
    if (field == "histogram" && partIdx + 1 < parts.size()) {
        int idx = std::stoi(parts[partIdx + 1]);
        if (idx >= 0 && idx < 8) return {true, static_cast<double>(fa.histogram[idx])};
    }

    return {false, 0.0};
}

std::pair<bool, double> resolvePath(const std::string& path,
                                    const ChainInspection& inspection) {
    auto parts = split(path, '.');
    if (parts.empty()) return {false, 0.0};

    // output.<field> -> FrameAnalysis
    if (parts[0] == "output" && parts.size() >= 2) {
        return resolveFrameAnalysisField(inspection.outputAnalysis, parts, 1);
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
    // operators.<name>.textureAnalysis.<field> -> FrameAnalysis
    if (parts[0] == "operators" && parts.size() >= 4) {
        const std::string& opName = parts[1];
        const std::string& category = parts[2];

        for (const auto& [name, data] : inspection.operators) {
            if (name == opName) {
                if (category == "metrics") {
                    const std::string& key = parts[3];
                    auto it = data.metrics.find(key);
                    if (it != data.metrics.end()) {
                        return {true, static_cast<double>(it->second)};
                    }
                }
                if (category == "textureAnalysis" && data.textureAnalysis.has_value()) {
                    return resolveFrameAnalysisField(*data.textureAnalysis, parts, 3);
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

static bool compareValues(double actual, const std::string& op, double expected, double expectedHigh = 0.0) {
    if (op == ">")  return actual > expected;
    if (op == ">=") return actual >= expected;
    if (op == "<")  return actual < expected;
    if (op == "<=") return actual <= expected;
    if (op == "==") return actual == expected;
    if (op == "!=") return actual != expected;
    if (op == "between") return actual >= expected && actual <= expectedHigh;
    return false;
}

CheckReport evaluateAssertions(const std::vector<Assertion>& assertions,
                               const ChainInspection& inspection, int frame) {
    CheckReport report;
    report.frame = frame;
    report.allPassed = true;

    for (const auto& a : assertions) {
        AssertionResult r;
        r.name = a.name;
        r.path = a.path;
        r.op = a.op;
        r.expected = a.value;
        r.expectedHigh = a.valueHigh;

        // Guard: after_frame — skip if current frame is before the threshold
        if (a.afterFrame >= 0 && frame < a.afterFrame) {
            r.skipped = true;
            r.skipReason = "frame " + std::to_string(frame) + " < after_frame " + std::to_string(a.afterFrame);
            report.results.push_back(std::move(r));
            continue;
        }

        // Guard: when_path — skip if the guard condition is not met
        if (!a.whenPath.empty() && !a.whenCheck.empty()) {
            auto [guardFound, guardActual] = resolvePath(a.whenPath, inspection);
            if (!guardFound || !compareValues(guardActual, a.whenCheck, a.whenValue)) {
                r.skipped = true;
                if (!guardFound) {
                    r.skipReason = "when_path '" + a.whenPath + "' not found";
                } else {
                    r.skipReason = "when condition not met: " + a.whenPath + " " + a.whenCheck + " " + std::to_string(a.whenValue) + " (actual: " + std::to_string(guardActual) + ")";
                }
                report.results.push_back(std::move(r));
                continue;
            }
        }

        // exists / not_exists: check path presence without numeric comparison
        if (a.op == "exists" || a.op == "not_exists") {
            auto [found, actual] = resolvePath(a.path, inspection);
            r.actual = found ? actual : 0.0;
            if (a.op == "exists") {
                r.passed = found;
            } else {
                r.passed = !found;
            }
            if (!r.passed) {
                r.message = a.message.empty()
                    ? (a.op == "exists" ? ("Path not found: " + a.path)
                                        : ("Path exists: " + a.path))
                    : a.message;
            }
            if (!r.passed) report.allPassed = false;
            report.results.push_back(std::move(r));
            continue;
        }

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
            r.passed = compareValues(actual, a.op, a.value, a.valueHigh);
            if (!r.passed) {
                if (!a.message.empty()) {
                    r.message = a.message;
                } else {
                    std::ostringstream ss;
                    if (a.op == "between") {
                        ss << a.path << ": expected between [" << a.value
                           << ", " << a.valueHigh << "], got " << actual;
                    } else {
                        ss << a.path << ": expected " << a.op << " " << a.value
                           << ", got " << actual;
                    }
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
        if (r.skipped) {
            rj["skipped"] = true;
            rj["skipReason"] = r.skipReason;
        } else {
            rj["passed"] = r.passed;
        }
        if (!r.name.empty()) {
            rj["name"] = r.name;
        }
        rj["path"] = r.path;
        rj["op"] = r.op;
        if (!r.skipped) {
            rj["expected"] = r.expected;
            if (r.op == "between") {
                rj["expectedHigh"] = r.expectedHigh;
            }
            rj["actual"] = r.actual;
        }
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
        if (r.skipped) {
            ss << "SKIP  ";
        } else {
            ss << (r.passed ? "PASS" : "FAIL") << "  ";
        }
        if (!r.name.empty()) {
            ss << r.name << "  ";
        }
        ss << r.path << " " << r.op;
        if (!r.skipped) {
            if (r.op == "between") {
                ss << " [" << r.expected << ", " << r.expectedHigh << "]";
            } else if (r.op != "exists" && r.op != "not_exists") {
                ss << " " << r.expected;
            }
            ss << " (actual: " << r.actual << ")";
        }
        if (r.skipped && !r.skipReason.empty()) {
            ss << "  -- " << r.skipReason;
        } else if (!r.passed && !r.message.empty()) {
            ss << "  -- " << r.message;
        }
        ss << "\n";
    }

    int passed = 0, failed = 0, skipped = 0;
    for (const auto& r : results) {
        if (r.skipped) skipped++;
        else if (r.passed) passed++;
        else failed++;
    }
    ss << "\n" << passed << " passed, " << failed << " failed";
    if (skipped > 0) {
        ss << ", " << skipped << " skipped";
    }
    ss << " (frame " << frame << ")\n";

    return ss.str();
}

} // namespace vivid
