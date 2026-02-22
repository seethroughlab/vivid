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
#include <cmath>
#include <limits>

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

    // --- Zero-cost extensions (computed in existing RMS loop) ---
    float dcOffset = 0.0f;          ///< Mean sample value. 0=ideal, >0.01=problematic
    int clippedSampleCount = 0;     ///< Number of samples at |s| >= 0.99
    float clippedSamplePct = 0.0f;  ///< Fraction of clipped samples
    float zeroCrossingRate = 0.0f;  ///< Zero crossings per second

    // --- Stereo metrics ---
    float stereoCorrelation = 1.0f; ///< L/R Pearson correlation, -1 to +1
    float stereoWidth = 0.0f;       ///< Side/mid RMS ratio (0=mono, higher=wider)

    // --- STFT spectral metrics ---
    float spectralCentroid = 0.0f;  ///< Brightness indicator in Hz
    float spectralSpread = 0.0f;    ///< Spectral bandwidth in Hz
    float spectralFlux = 0.0f;      ///< Mean spectral change (normalized)
    float spectralFluxMax = 0.0f;   ///< Max spectral flux (onset strength)
    float spectralFlatness = 0.0f;  ///< 0=tonal, 1=noise-like
    float spectralRolloff = 0.0f;   ///< 85th percentile frequency in Hz

    // --- Onset detection ---
    float onsetDensity = 0.0f;      ///< Onsets per second
    int onsetCount = 0;             ///< Total onsets detected

    // --- LUFS loudness (EBU R128) ---
    float integratedLUFS = -std::numeric_limits<float>::infinity();   ///< Integrated loudness
    float shortTermLUFS = -std::numeric_limits<float>::infinity();    ///< Short-term (3s window)
    float momentaryLUFS = -std::numeric_limits<float>::infinity();    ///< Momentary (400ms)
    float truePeak = 0.0f;          ///< True peak amplitude (4x oversampled)
    float truePeakDBTP = -std::numeric_limits<float>::infinity();     ///< True peak in dBTP
    float loudnessRange = 0.0f;     ///< LRA: 10th-95th percentile of short-term

    // --- Pitch detection (YIN) ---
    float pitchHz = 0.0f;           ///< Fundamental frequency in Hz
    float pitchConfidence = 0.0f;   ///< YIN confidence (0-1, >0.8 = reliable)
    std::string pitchNote;          ///< Note name (e.g. "A4", "C#3")
    float pitchCents = 0.0f;        ///< Cents deviation from nearest note

    // --- Harmonic to noise ratio ---
    float harmonicToNoiseRatio = 0.0f;  ///< HNR in dB (sine>15, noise<5)

    // --- Dynamic range ---
    float dynamicRangeDB = 0.0f;        ///< Max-min block RMS in dB
    float dynamicRangeCoeffVar = 0.0f;  ///< Coefficient of variation of block RMS

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
