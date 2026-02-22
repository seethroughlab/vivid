#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace vivid {

/// A single actionable hint about a metric that's out of range
struct AnalysisHint {
    std::string metric;      ///< Field name (e.g. "meanBrightness")
    double value;            ///< Actual value observed
    std::string status;      ///< "low", "high", or "warning"
    std::array<double, 2> range; ///< [min, max] healthy range
    std::string suggestion;  ///< Actionable advice (<=120 chars)
    int severity;            ///< Priority: 0=critical, 1=important, 2=minor
};

/// Generate hints from visual FrameAnalysis metrics (output.*)
std::vector<AnalysisHint> generateVisualHints(const nlohmann::json& output);

/// Generate hints from AudioAnalysis metrics (audio.*)
std::vector<AnalysisHint> generateAudioHints(const nlohmann::json& audio);

/// Generate hints from TemporalAnalysis metrics (temporal.*)
std::vector<AnalysisHint> generateTemporalHints(const nlohmann::json& temporal);

/// Generate hints from AudioVisualAnalysis metrics (av.*)
std::vector<AnalysisHint> generateAVHints(const nlohmann::json& av);

/// Collect hints from all available sections, prioritize by severity, cap at maxHints
/// Returns a JSON array suitable for inclusion in MCP responses
nlohmann::json collectHints(const nlohmann::json& response, int maxHints = 5);

} // namespace vivid
