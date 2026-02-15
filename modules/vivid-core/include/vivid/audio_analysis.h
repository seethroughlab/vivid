#pragma once

/**
 * @file audio_analysis.h
 * @brief Audio buffer analysis for LLM-driven audio evaluation
 *
 * AudioAnalysis provides statistical analysis of an audio buffer:
 * RMS level, peak level, crest factor, 6-band spectrum, and silence detection.
 * The audio equivalent of FrameAnalysis for visual textures.
 */

#include <array>
#include <string>
#include <cstdint>

namespace vivid {

/**
 * @brief Statistical analysis of an audio buffer
 *
 * Parallel to FrameAnalysis for visual textures. Provides metrics
 * an LLM can use to evaluate audio output without listening.
 */
struct AudioAnalysis {
    float rmsLevel = 0.0f;          ///< Overall RMS (0-1)
    float peakLevel = 0.0f;         ///< Overall peak (0-1)
    float rmsLeft = 0.0f;           ///< Left channel RMS
    float rmsRight = 0.0f;          ///< Right channel RMS
    bool isSilent = true;           ///< RMS < 0.001
    float crestFactor = 0.0f;       ///< peak/rms (dynamics indicator)

    /// 6-band spectrum energy: subBass, bass, lowMid, mid, highMid, high
    /// Frequency splits: <60Hz, 60-250, 250-500, 500-2k, 2k-4k, 4k+
    std::array<float, 6> spectrum = {};

    float duration = 0.0f;          ///< Buffer duration in seconds

    std::string toJSON() const;
};

/**
 * @brief Analyze an audio buffer and produce statistics
 *
 * @param samples Interleaved float samples [-1.0, 1.0]
 * @param frameCount Number of frames (samples per channel)
 * @param channels Number of channels (1=mono, 2=stereo)
 * @param sampleRate Sample rate in Hz
 * @return AudioAnalysis with computed statistics
 */
AudioAnalysis analyzeAudioBuffer(const float* samples, uint32_t frameCount,
                                  uint32_t channels, uint32_t sampleRate);

} // namespace vivid
