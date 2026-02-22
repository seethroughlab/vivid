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

    // Tier 1 additions
    if (field == "textureEntropy") return {true, fa.textureEntropy};
    if (field == "edgeDensity") return {true, fa.edgeDensity};
    if (field == "avgGradientMag") return {true, fa.avgGradientMag};
    if (field == "clipBlackPct") return {true, fa.clipBlackPct};
    if (field == "clipWhitePct") return {true, fa.clipWhitePct};
    if (field == "headroom") return {true, fa.headroom};
    if (field == "rangeSpan") return {true, fa.rangeSpan};
    if (field == "sharpness") return {true, fa.sharpness};
    if (field == "noiseLevel") return {true, fa.noiseLevel};
    if (field == "visualCenterX") return {true, fa.visualCenterX};
    if (field == "visualCenterY") return {true, fa.visualCenterY};
    if (field == "colorTemperature") return {true, fa.colorTemperature};
    if (field == "uniqueHueCount") return {true, static_cast<double>(fa.uniqueHueCount)};
    if (field == "hueEntropy") return {true, fa.hueEntropy};
    if (field == "alphaOpaquePct") return {true, fa.alphaOpaquePct};
    if (field == "alphaTransparentPct") return {true, fa.alphaTransparentPct};
    if (field == "alphaPartialPct") return {true, fa.alphaPartialPct};
    if (field == "alphaMean") return {true, fa.alphaMean};

    // hueHistogram.0 through hueHistogram.11
    if (field == "hueHistogram" && partIdx + 1 < parts.size()) {
        int idx = std::stoi(parts[partIdx + 1]);
        if (idx >= 0 && idx < 12) return {true, fa.hueHistogram[idx]};
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
        if (parts[1] == "dcOffset") return {true, aa.dcOffset};
        if (parts[1] == "clippedSampleCount") return {true, static_cast<double>(aa.clippedSampleCount)};
        if (parts[1] == "clippedSamplePct") return {true, aa.clippedSamplePct};
        if (parts[1] == "zeroCrossingRate") return {true, aa.zeroCrossingRate};
        if (parts[1] == "stereoCorrelation") return {true, aa.stereoCorrelation};
        if (parts[1] == "stereoWidth") return {true, aa.stereoWidth};
        if (parts[1] == "spectralCentroid") return {true, aa.spectralCentroid};
        if (parts[1] == "spectralSpread") return {true, aa.spectralSpread};
        if (parts[1] == "spectralFlux") return {true, aa.spectralFlux};
        if (parts[1] == "spectralFluxMax") return {true, aa.spectralFluxMax};
        if (parts[1] == "spectralFlatness") return {true, aa.spectralFlatness};
        if (parts[1] == "spectralRolloff") return {true, aa.spectralRolloff};
        if (parts[1] == "onsetDensity") return {true, aa.onsetDensity};
        if (parts[1] == "onsetCount") return {true, static_cast<double>(aa.onsetCount)};
        if (parts[1] == "integratedLUFS") return {true, aa.integratedLUFS};
        if (parts[1] == "shortTermLUFS") return {true, aa.shortTermLUFS};
        if (parts[1] == "momentaryLUFS") return {true, aa.momentaryLUFS};
        if (parts[1] == "truePeak") return {true, aa.truePeak};
        if (parts[1] == "truePeakDBTP") return {true, aa.truePeakDBTP};
        if (parts[1] == "loudnessRange") return {true, aa.loudnessRange};
        if (parts[1] == "pitchHz") return {true, aa.pitchHz};
        if (parts[1] == "pitchConfidence") return {true, aa.pitchConfidence};
        if (parts[1] == "pitchCents") return {true, aa.pitchCents};
        if (parts[1] == "harmonicToNoiseRatio") return {true, aa.harmonicToNoiseRatio};
        if (parts[1] == "dynamicRangeDB") return {true, aa.dynamicRangeDB};
        if (parts[1] == "dynamicRangeCoeffVar") return {true, aa.dynamicRangeCoeffVar};

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

    // temporal.<field> -> TemporalAnalysis
    if (parts[0] == "temporal" && parts.size() >= 2 && inspection.temporalAnalysis.has_value()) {
        const auto& ta = *inspection.temporalAnalysis;
        if (parts[1] == "flickerScore") return {true, ta.flickerScore};
        if (parts[1] == "flickerFrequency") return {true, ta.flickerFrequency};
        if (parts[1] == "frameDelta") return {true, ta.frameDelta};
        if (parts[1] == "convergenceScore") return {true, ta.convergenceScore};
        if (parts[1] == "isConverged") return {true, ta.isConverged ? 1.0 : 0.0};
        if (parts[1] == "motionMagnitude") return {true, ta.motionMagnitude};
        if (parts[1] == "frameDiversity") return {true, ta.frameDiversity};
        if (parts[1] == "isFrozen") return {true, ta.isFrozen ? 1.0 : 0.0};
        if (parts[1] == "isLooping") return {true, ta.isLooping ? 1.0 : 0.0};
        if (parts[1] == "loopPeriodSeconds") return {true, ta.loopPeriodSeconds};
        if (parts[1] == "loopPeriodFrames") return {true, static_cast<double>(ta.loopPeriodFrames)};
        if (parts[1] == "loopConfidence") return {true, ta.loopConfidence};
        if (parts[1] == "noveltyScore") return {true, ta.noveltyScore};
        if (parts[1] == "noveltyTrend") return {true, ta.noveltyTrend};
        if (parts[1] == "keyframeCount") return {true, static_cast<double>(ta.keyframeCount)};
        if (parts[1] == "regionMotion" && parts.size() >= 3) {
            int idx = std::stoi(parts[2]);
            if (idx >= 0 && idx < 9) return {true, ta.regionMotion[idx]};
        }
        return {false, 0.0};
    }

    // av.<field> -> AudioVisualAnalysis
    if (parts[0] == "av" && parts.size() >= 2 && inspection.avAnalysis.has_value()) {
        const auto& av = *inspection.avAnalysis;
        if (parts[1] == "correlation") return {true, av.avCorrelation};
        if (parts[1] == "correlationBrightness") return {true, av.avCorrelationBrightness};
        if (parts[1] == "correlationMotion") return {true, av.avCorrelationMotion};
        if (parts[1] == "reactivityLatencyFrames") return {true, static_cast<double>(av.reactivityLatencyFrames)};
        if (parts[1] == "reactivityLatencyMs") return {true, av.reactivityLatencyMs};
        if (parts[1] == "reactivityPeakCorrelation") return {true, av.reactivityPeakCorrelation};
        if (parts[1] == "onsetResponseRate") return {true, av.onsetResponseRate};
        if (parts[1] == "onsetResponseCount") return {true, static_cast<double>(av.onsetResponseCount)};
        if (parts[1] == "totalOnsetsEvaluated") return {true, static_cast<double>(av.totalOnsetsEvaluated)};
        if (parts[1] == "responseMagnitude") return {true, av.responseMagnitude};
        if (parts[1] == "responseMagnitudeRatio") return {true, av.responseMagnitudeRatio};
        if (parts[1] == "avgPostOnsetDelta") return {true, av.avgPostOnsetDelta};
        if (parts[1] == "avgBaselineDelta") return {true, av.avgBaselineDelta};
        if (parts[1] == "mutualInformation") return {true, av.avMutualInformation};
        if (parts[1] == "mutualInformationRaw") return {true, av.avMutualInformationRaw};
        if (parts[1] == "sampleCount") return {true, static_cast<double>(av.sampleCount)};
        if (parts[1] == "durationSeconds") return {true, av.durationSeconds};
        if (parts[1] == "valid") return {true, av.valid ? 1.0 : 0.0};

        // av.bandCorrelation.<bandName> -> per-band correlation (absolute value)
        if (parts[1] == "bandCorrelation" && parts.size() >= 3) {
            int bandIdx = -1;
            if (parts[2] == "subBass")  bandIdx = 0;
            else if (parts[2] == "bass")     bandIdx = 1;
            else if (parts[2] == "lowMid")   bandIdx = 2;
            else if (parts[2] == "mid")      bandIdx = 3;
            else if (parts[2] == "highMid")  bandIdx = 4;
            else if (parts[2] == "high")     bandIdx = 5;
            if (bandIdx >= 0) return {true, av.bandCorrelations[bandIdx].correlation};
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
