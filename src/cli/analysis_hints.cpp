// Analysis Hints — actionable guidance for LLMs when metrics are out of range
// Generates brief suggestions attached to MCP tool responses (inspect_chain,
// capture_audio, analyze_av_reactivity).

#include <vivid/analysis_hints.h>
#include <algorithm>
#include <cmath>

namespace vivid {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static double getDouble(const nlohmann::json& j, const std::string& key, double fallback) {
    if (j.contains(key) && j[key].is_number()) return j[key].get<double>();
    return fallback;
}

static bool getBool(const nlohmann::json& j, const std::string& key, bool fallback) {
    if (j.contains(key) && j[key].is_boolean()) return j[key].get<bool>();
    return fallback;
}

static void pushHint(std::vector<AnalysisHint>& hints,
                     const std::string& metric, double value,
                     const std::string& status,
                     double lo, double hi,
                     const std::string& suggestion,
                     int severity) {
    hints.push_back({metric, value, status, {lo, hi}, suggestion, severity});
}

// ---------------------------------------------------------------------------
// Visual (FrameAnalysis) hints
// ---------------------------------------------------------------------------

std::vector<AnalysisHint> generateVisualHints(const nlohmann::json& output) {
    std::vector<AnalysisHint> hints;
    if (output.is_null() || !output.is_object()) return hints;

    // meanBrightness
    double brightness = getDouble(output, "meanBrightness", -1);
    if (brightness >= 0) {
        if (brightness < 0.15)
            pushHint(hints, "meanBrightness", brightness, "low", 0.15, 0.85,
                     "Too dark. Try: Brightness (gain > 1.0), Levels, or check input textures.", 1);
        else if (brightness > 0.85)
            pushHint(hints, "meanBrightness", brightness, "high", 0.15, 0.85,
                     "Too bright. Try: Brightness (gain < 1.0), Levels (pull highlights), or Exposure.", 1);
    }

    // contrast
    double contrast = getDouble(output, "contrast", -1);
    if (contrast >= 0) {
        if (contrast < 0.1)
            pushHint(hints, "contrast", contrast, "low", 0.1, 0.4,
                     "Very flat. Try: Levels, Contrast operator, or increase input variety.", 1);
        else if (contrast > 0.4)
            pushHint(hints, "contrast", contrast, "high", 0.1, 0.4,
                     "Harsh contrast. Try: Levels (compress range) or reduce effect intensity.", 2);
    }

    // clipBlackPct
    double clipBlack = getDouble(output, "clipBlackPct", -1);
    if (clipBlack > 0.1)
        pushHint(hints, "clipBlackPct", clipBlack, "high", 0.0, 0.1,
                 "Crushed blacks. Try: Levels (lift shadows) or per_operator_analysis to find dark source.", 1);

    // clipWhitePct
    double clipWhite = getDouble(output, "clipWhitePct", -1);
    if (clipWhite > 0.1)
        pushHint(hints, "clipWhitePct", clipWhite, "high", 0.0, 0.1,
                 "Blown highlights. Try: Levels (pull highlights) or reduce Brightness gain.", 1);

    // textureEntropy
    double entropy = getDouble(output, "textureEntropy", -1);
    if (entropy >= 0 && entropy < 0.15)
        pushHint(hints, "textureEntropy", entropy, "low", 0.15, 1.0,
                 "Very flat/uniform. Try: add Noise, increase pattern complexity, or check inputs.", 2);

    // sharpness
    double sharpness = getDouble(output, "sharpness", -1);
    if (sharpness >= 0 && sharpness < 0.005)
        pushHint(hints, "sharpness", sharpness, "low", 0.005, 1.0,
                 "Very blurry. Try: reduce Blur radius, add Sharpen, or increase resolution.", 2);

    // colorTemperature
    double temp = getDouble(output, "colorTemperature", -1);
    if (temp >= 0) {
        if (temp < 0.2)
            pushHint(hints, "colorTemperature", temp, "low", 0.2, 0.8,
                     "Very cold/blue. Try: ColorCorrect (warm), or adjust color balance.", 2);
        else if (temp > 0.8)
            pushHint(hints, "colorTemperature", temp, "high", 0.2, 0.8,
                     "Very warm/orange. Try: ColorCorrect (cool), or adjust color balance.", 2);
    }

    // edgeDensity
    double edges = getDouble(output, "edgeDensity", -1);
    if (edges >= 0 && edges < 0.02)
        pushHint(hints, "edgeDensity", edges, "low", 0.02, 1.0,
                 "No detail/edges. Check if operators are producing visible output.", 2);

    return hints;
}

// ---------------------------------------------------------------------------
// Audio hints
// ---------------------------------------------------------------------------

std::vector<AnalysisHint> generateAudioHints(const nlohmann::json& audio) {
    std::vector<AnalysisHint> hints;
    if (audio.is_null() || !audio.is_object()) return hints;

    // isSilent — highest priority
    bool silent = getBool(audio, "isSilent", false);
    if (silent) {
        pushHint(hints, "isSilent", 1.0, "warning", 0.0, 0.0,
                 "Audio is silent. Check: AudioOutput connected, oscillator frequency > 0, gain > 0.", 0);
        return hints;  // No point checking other metrics if silent
    }

    // rmsLevel
    double rms = getDouble(audio, "rmsLevel", -1);
    if (rms >= 0 && rms < 0.01)
        pushHint(hints, "rmsLevel", rms, "low", 0.01, 0.9,
                 "Nearly silent. Try: increase Gain, check oscillator amplitude, verify audio routing.", 0);

    // clippedSamplePct
    double clipped = getDouble(audio, "clippedSamplePct", -1);
    if (clipped > 0.01)
        pushHint(hints, "clippedSamplePct", clipped, "high", 0.0, 0.01,
                 "Audio clipping detected. Try: reduce Gain, add Limiter, or lower oscillator amplitude.", 0);

    // dcOffset
    double dc = getDouble(audio, "dcOffset", -1);
    if (dc >= 0 && std::abs(dc) > 0.05)
        pushHint(hints, "dcOffset", dc, "high", -0.05, 0.05,
                 "DC offset detected. Try: add a high-pass filter or check for asymmetric waveforms.", 1);

    // integratedLUFS
    double lufs = getDouble(audio, "integratedLUFS", 0);
    if (audio.contains("integratedLUFS") && lufs < -40)
        pushHint(hints, "integratedLUFS", lufs, "low", -40.0, 0.0,
                 "Very quiet. Try: increase Gain or oscillator amplitude. Target ~-23 LUFS for broadcast.", 1);

    return hints;
}

// ---------------------------------------------------------------------------
// Temporal hints
// ---------------------------------------------------------------------------

std::vector<AnalysisHint> generateTemporalHints(const nlohmann::json& temporal) {
    std::vector<AnalysisHint> hints;
    if (temporal.is_null() || !temporal.is_object()) return hints;

    // isFrozen — highest priority
    bool frozen = getBool(temporal, "isFrozen", false);
    if (frozen)
        pushHint(hints, "isFrozen", 1.0, "warning", 0.0, 0.0,
                 "Output is frozen. Check: time-varying params (speed > 0), animation expressions, input sources.", 0);

    // flickerScore
    double flicker = getDouble(temporal, "flickerScore", -1);
    if (flicker > 0.5)
        pushHint(hints, "flickerScore", flicker, "high", 0.0, 0.5,
                 "Excessive flicker. Try: reduce speed, add temporal smoothing, or check for discontinuities.", 1);

    return hints;
}

// ---------------------------------------------------------------------------
// AV Reactivity hints
// ---------------------------------------------------------------------------

std::vector<AnalysisHint> generateAVHints(const nlohmann::json& av) {
    std::vector<AnalysisHint> hints;
    if (av.is_null() || !av.is_object()) return hints;

    // Skip if explicitly marked invalid
    if (av.contains("invalidReason")) return hints;
    // Also skip if isValid is false
    if (getBool(av, "isValid", true) == false) return hints;

    // avCorrelation (called "correlation" in some responses)
    double corr = getDouble(av, "correlation", -1);
    if (corr < 0) corr = getDouble(av, "avCorrelation", -1);
    if (corr >= 0 && corr < 0.1)
        pushHint(hints, "avCorrelation", corr, "low", 0.1, 1.0,
                 "Low AV correlation. Check: audio input connected to visual params, reactivity mapping.", 1);

    // onsetResponseRate
    double onset = getDouble(av, "onsetResponseRate", -1);
    if (onset >= 0 && onset < 0.2)
        pushHint(hints, "onsetResponseRate", onset, "low", 0.2, 1.0,
                 "Visuals barely respond to beats. Try: map audio onset/FFT to visual params more strongly.", 1);

    // reactivityLatencyMs
    double latency = getDouble(av, "reactivityLatencyMs", -1);
    if (latency > 200)
        pushHint(hints, "reactivityLatencyMs", latency, "high", 0.0, 200.0,
                 "High AV latency. Try: reduce audio buffer size, simplify chain, check processing order.", 2);

    return hints;
}

// ---------------------------------------------------------------------------
// Collect and prioritize
// ---------------------------------------------------------------------------

static nlohmann::json hintToJson(const AnalysisHint& h) {
    nlohmann::json j;
    j["metric"] = h.metric;
    j["value"] = h.value;
    j["status"] = h.status;
    j["range"] = {h.range[0], h.range[1]};
    j["suggestion"] = h.suggestion;
    return j;
}

nlohmann::json collectHints(const nlohmann::json& response, int maxHints) {
    std::vector<AnalysisHint> all;

    // Gather from all available sections
    // inspect_chain nests under "inspection", capture_audio under "analysis"
    nlohmann::json inspection;
    if (response.contains("inspection")) inspection = response["inspection"];
    else inspection = response;  // fallback: response IS the inspection

    if (inspection.contains("output"))
        for (auto& h : generateVisualHints(inspection["output"])) all.push_back(h);

    if (inspection.contains("audio"))
        for (auto& h : generateAudioHints(inspection["audio"])) all.push_back(h);
    else if (response.contains("analysis"))
        for (auto& h : generateAudioHints(response["analysis"])) all.push_back(h);

    if (inspection.contains("temporal"))
        for (auto& h : generateTemporalHints(inspection["temporal"])) all.push_back(h);

    if (inspection.contains("audioVisual"))
        for (auto& h : generateAVHints(inspection["audioVisual"])) all.push_back(h);
    else if (response.contains("analysis"))
        for (auto& h : generateAVHints(response["analysis"])) all.push_back(h);

    if (all.empty()) return nlohmann::json::array();

    // Sort by severity (lower = more critical)
    std::stable_sort(all.begin(), all.end(),
                     [](const AnalysisHint& a, const AnalysisHint& b) {
                         return a.severity < b.severity;
                     });

    // Cap at maxHints
    if (static_cast<int>(all.size()) > maxHints)
        all.resize(maxHints);

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& h : all)
        arr.push_back(hintToJson(h));
    return arr;
}

} // namespace vivid
