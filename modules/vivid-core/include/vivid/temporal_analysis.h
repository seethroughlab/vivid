#pragma once

/**
 * @file temporal_analysis.h
 * @brief Multi-frame temporal analysis for LLM-driven autonomous evaluation
 *
 * TemporalAnalyzer maintains a ring buffer of downsampled frames and computes
 * cross-frame metrics: flicker, convergence, motion, diversity, loop detection,
 * and visual novelty.
 */

#include <array>
#include <string>
#include <sstream>
#include <vector>
#include <cstdint>

namespace vivid {

/**
 * @brief Temporal analysis results computed from multiple frames
 */
struct TemporalAnalysis {
    // --- Flicker ---
    float flickerScore = 0.0f;       ///< 0-1, high-frequency brightness oscillation
    float flickerFrequency = 0.0f;   ///< Dominant flicker frequency in Hz (0 = no flicker)

    // --- Convergence ---
    float frameDelta = 0.0f;         ///< Mean absolute luminance change vs previous frame (0-1)
    float convergenceScore = 0.0f;   ///< 0 = diverging, 0.5 = stable, 1 = converged
    bool isConverged = false;        ///< frameDelta < threshold for N frames

    // --- Motion ---
    float motionMagnitude = 0.0f;    ///< Average pixel displacement (0 = still)
    std::array<float, 9> regionMotion = {};  ///< 3x3 spatial motion grid

    // --- Diversity ---
    float frameDiversity = 0.0f;     ///< Variance of inter-frame deltas
    bool isFrozen = false;           ///< No change detected for N frames

    // --- Loop detection ---
    bool isLooping = false;            ///< Periodic repetition detected
    float loopPeriodSeconds = 0.0f;    ///< Estimated loop duration (0 = not looping)
    int loopPeriodFrames = 0;          ///< Loop duration in frames
    float loopConfidence = 0.0f;       ///< Autocorrelation peak strength (0-1)

    // --- Visual Novelty ---
    float noveltyScore = 0.0f;         ///< Mean distance from current frame to keyframes (0-1)
    float noveltyTrend = 0.5f;         ///< 0 = collapsing, 0.5 = stable, 1 = exploring
    int keyframeCount = 0;             ///< Number of keyframes in buffer

    std::string toJSON() const {
        std::ostringstream ss;
        ss << "{";
        ss << "\"flickerScore\":" << flickerScore;
        ss << ",\"flickerFrequency\":" << flickerFrequency;
        ss << ",\"frameDelta\":" << frameDelta;
        ss << ",\"convergenceScore\":" << convergenceScore;
        ss << ",\"isConverged\":" << (isConverged ? "true" : "false");
        ss << ",\"motionMagnitude\":" << motionMagnitude;
        ss << ",\"regionMotion\":[";
        for (int i = 0; i < 9; i++) {
            if (i > 0) ss << ",";
            ss << regionMotion[i];
        }
        ss << "]";
        ss << ",\"frameDiversity\":" << frameDiversity;
        ss << ",\"isFrozen\":" << (isFrozen ? "true" : "false");
        ss << ",\"isLooping\":" << (isLooping ? "true" : "false");
        ss << ",\"loopPeriodSeconds\":" << loopPeriodSeconds;
        ss << ",\"loopPeriodFrames\":" << loopPeriodFrames;
        ss << ",\"loopConfidence\":" << loopConfidence;
        ss << ",\"noveltyScore\":" << noveltyScore;
        ss << ",\"noveltyTrend\":" << noveltyTrend;
        ss << ",\"keyframeCount\":" << keyframeCount;
        ss << "}";
        return ss.str();
    }
};

/**
 * @brief Stateful analyzer that accumulates frames and computes temporal metrics
 */
class TemporalAnalyzer {
public:
    struct Config {
        int windowSize = 16;              ///< Luminance ring buffer size
        int brightnessHistorySize = 120;  ///< Brightness history for loop detection
        int downsampleWidth = 64;         ///< Analysis resolution
        int downsampleHeight = 64;
        float fps = 60.0f;               ///< Assumed frame rate
        int keyframeInterval = 30;        ///< Sample a keyframe every N frames
        int keyframeBufferSize = 8;       ///< Max keyframes stored
    };

    TemporalAnalyzer() = default;
    explicit TemporalAnalyzer(const Config& config) : m_config(config) {}

    /// Feed a new frame (RGBA8 pixels, full resolution)
    void pushFrame(const uint8_t* pixels, int width, int height);

    /// Get temporal analysis (only valid after >= 2 frames pushed)
    TemporalAnalysis analyze() const;

    /// Reset all state
    void reset();

    /// Enable sparse mode (disables flicker detection when frames are non-consecutive)
    void setSparseMode(bool sparse) { m_sparseMode = sparse; }

    /// Get number of frames pushed
    int frameCount() const { return m_frameCount; }

private:
    Config m_config;

    // Ring buffer of 64x64 luminance frames
    std::vector<std::vector<float>> m_luminanceRing;

    // Per-frame mean brightness history (extended for loop detection)
    std::vector<float> m_brightnessHistory;

    // Per-frame delta history
    std::vector<float> m_deltaHistory;

    // Keyframe buffer for novelty detection
    std::vector<std::vector<float>> m_keyframes;

    // Novelty score history
    std::vector<float> m_noveltyHistory;

    int m_frameCount = 0;
    int m_writeIndex = 0;         // Ring buffer write index for luminance frames
    int m_brightnessWriteIndex = 0;
    int m_deltaWriteIndex = 0;
    int m_keyframeWriteIndex = 0;
    int m_noveltyWriteIndex = 0;
    bool m_sparseMode = false;

    // Convergence tracking
    float m_smoothDelta = 0.0f;
    int m_convergedFrames = 0;

    // Helper: downsample RGBA8 pixels to luminance buffer
    void downsampleToLuminance(const uint8_t* pixels, int width, int height,
                                std::vector<float>& out) const;
};

} // namespace vivid
