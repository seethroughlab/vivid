#pragma once

/**
 * @file av_analysis.h
 * @brief Audio-visual cross-domain reactivity analysis
 *
 * Measures whether visuals respond to audio by correlating synchronized
 * audio + visual time-series. Three tiers of metrics:
 * - Tier 1: Pearson correlation (audio RMS vs brightness/motion, per-band, latency)
 * - Tier 2: Event-based (onset response rate, response magnitude)
 * - Tier 3: Information-theoretic (mutual information)
 */

#include <array>
#include <string>
#include <vector>
#include <cstdint>

namespace vivid {

/**
 * @brief Cross-domain audio-visual reactivity metrics
 */
struct AudioVisualAnalysis {
    // --- Tier 1: Cross-Correlation ---
    float avCorrelation = 0.0f;            ///< Overall AV correlation (0-1, best absolute correlation)
    float avCorrelationBrightness = 0.0f;  ///< audio.rms vs visual.brightness (-1 to +1)
    float avCorrelationMotion = 0.0f;      ///< audio.rms vs visual.motionMagnitude (-1 to +1)

    struct BandCorrelation {
        float correlation = 0.0f;     ///< Strongest |correlation| for this band (0-1)
        std::string drivesMetric;     ///< Which visual metric ("brightness", "motion", "contrast")
        float rawCorrelation = 0.0f;  ///< Signed correlation value (-1 to +1)
    };
    std::array<BandCorrelation, 6> bandCorrelations = {};  ///< Per-band: subBass, bass, lowMid, mid, highMid, high

    int reactivityLatencyFrames = 0;          ///< Optimal lag in frames (positive = visual lags audio)
    float reactivityLatencyMs = 0.0f;         ///< Optimal lag in milliseconds
    float reactivityPeakCorrelation = 0.0f;   ///< Correlation at optimal lag

    // --- Tier 2: Event-Based ---
    float onsetResponseRate = 0.0f;     ///< Fraction of audio onsets with visual response (0-1)
    int onsetResponseCount = 0;          ///< Number of onsets that produced visual response
    int totalOnsetsEvaluated = 0;        ///< Total onsets with complete response windows
    float responseMagnitude = 0.0f;      ///< Mean post-onset delta minus baseline delta
    float responseMagnitudeRatio = 1.0f; ///< Post-onset delta / baseline delta (>1 = responsive)
    float avgPostOnsetDelta = 0.0f;      ///< Mean frameDelta in frames after onsets
    float avgBaselineDelta = 0.0f;       ///< Mean frameDelta in frames without onsets

    // --- Tier 3: Information-Theoretic ---
    float avMutualInformation = 0.0f;      ///< Normalized mutual information (0-1)
    float avMutualInformationRaw = 0.0f;   ///< Raw MI in bits

    // --- Metadata ---
    int sampleCount = 0;          ///< Number of synchronized AV samples used
    float durationSeconds = 0.0f; ///< Capture duration
    bool valid = false;           ///< False if insufficient data for any metric
    std::string invalidReason;    ///< Why analysis is invalid

    std::string toJSON() const;
};

/**
 * @brief Accumulates synchronized audio-visual samples and computes cross-domain metrics
 *
 * Operates on pre-extracted metric values (not raw pixels or audio buffers).
 * Callers push Sample structs with per-frame audio and visual state, then
 * call analyze() to get the full AudioVisualAnalysis.
 */
class AudioVisualAnalyzer {
public:
    struct Sample {
        float audioRms = 0.0f;                    ///< Audio RMS level
        std::array<float, 6> audioSpectrum = {};   ///< 6-band spectrum energy
        float brightness = 0.0f;                   ///< Visual mean brightness
        float motionMagnitude = 0.0f;              ///< Brightness delta between consecutive samples
        float contrast = 0.0f;                     ///< Visual contrast
        float frameDelta = 0.0f;                   ///< Per-pixel frame change
        bool isOnset = false;                      ///< Audio onset detected at this sample
    };

    struct Config {
        float fps = 60.0f;                ///< Assumed frame rate (for latency ms conversion)
        int maxLag = 10;                  ///< Maximum lag frames for cross-correlation
        int responseWindowFrames = 5;     ///< Frames after onset to check for visual response
        float responseThreshold = 0.02f;  ///< frameDelta threshold for "visual response"
        int minSamples = 10;             ///< Minimum samples for valid analysis
        int miBins = 8;                  ///< Quantization bins for mutual information
    };

    AudioVisualAnalyzer() = default;
    explicit AudioVisualAnalyzer(const Config& config) : m_config(config) {}

    /// Feed a synchronized audio-visual sample
    void pushSample(const Sample& sample);

    /// Compute full AV analysis from accumulated samples
    AudioVisualAnalysis analyze() const;

    /// Reset all state
    void reset();

    /// Number of accumulated samples
    int sampleCount() const { return static_cast<int>(m_samples.size()); }

private:
    Config m_config;
    std::vector<Sample> m_samples;
};

} // namespace vivid
