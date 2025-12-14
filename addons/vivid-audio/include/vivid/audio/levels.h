#pragma once

/**
 * @file levels.h
 * @brief RMS and peak level analysis
 *
 * Levels provides real-time amplitude analysis:
 * - RMS (root mean square) for average loudness
 * - Peak for maximum amplitude
 * - Smoothing for stable readings
 */

#include <vivid/audio/audio_analyzer.h>
#include <vivid/param.h>

namespace vivid::audio {

/**
 * @brief Amplitude level analyzer
 *
 * Computes RMS and peak levels from audio input.
 * Values are smoothed for stable visual feedback.
 *
 * @par Example
 * @code
 * chain.add<Levels>("levels").input("audio").smoothing(0.9f);
 *
 * // In update():
 * float volume = chain.get<Levels>("levels").rms();
 * chain.get<Noise>("noise").scale(1.0f + volume * 10.0f);
 * @endcode
 */
class Levels : public AudioAnalyzer {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> smoothing{"smoothing", 0.9f, 0.0f, 0.999f};  ///< Smoothing factor

    /// @}
    // -------------------------------------------------------------------------

    Levels() {
        registerParam(smoothing);
    }

    // -------------------------------------------------------------------------
    /// @name Analysis Results
    /// @{

    /**
     * @brief Get RMS level (0-1)
     *
     * Root mean square of the audio signal.
     * Represents average loudness.
     */
    float rms() const { return m_rms; }

    /**
     * @brief Get peak level (0-1)
     *
     * Maximum absolute sample value.
     * Useful for detecting transients.
     */
    float peak() const { return m_peak; }

    /**
     * @brief Get RMS in decibels
     * @return dB value (-inf to 0)
     */
    float rmsDb() const;

    /**
     * @brief Get peak in decibels
     * @return dB value (-inf to 0)
     */
    float peakDb() const;

    /**
     * @brief Get left channel RMS
     */
    float rmsLeft() const { return m_rmsLeft; }

    /**
     * @brief Get right channel RMS
     */
    float rmsRight() const { return m_rmsRight; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "Levels"; }

    /// @}

protected:
    void analyze(const float* input, uint32_t frames, uint32_t channels) override;

private:
    // Smoothed values
    float m_rms = 0.0f;
    float m_peak = 0.0f;
    float m_rmsLeft = 0.0f;
    float m_rmsRight = 0.0f;
};

} // namespace vivid::audio
