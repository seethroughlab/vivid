#pragma once

/**
 * @file beat_detect.h
 * @brief Beat and onset detection
 *
 * BeatDetect provides:
 * - Onset detection (beat triggers)
 * - Energy tracking with decay
 * - Sensitivity adjustment
 */

#include <vivid/audio/audio_analyzer.h>
#include <vector>

namespace vivid::audio {

/**
 * @brief Beat/onset detector
 *
 * Detects beats by comparing current energy to a rolling average.
 * When energy spikes above threshold, triggers a beat.
 *
 * @par Example
 * @code
 * chain.add<BeatDetect>("beat").input("audio").sensitivity(1.5f);
 *
 * // In update():
 * if (chain.get<BeatDetect>("beat").beat()) {
 *     // Flash on beat!
 *     chain.get<Gradient>("bg").colorA(1.0f, 1.0f, 1.0f);
 * }
 * float energy = chain.get<BeatDetect>("beat").energy();
 * @endcode
 */
class BeatDetect : public AudioAnalyzer {
public:
    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Connect to audio source
     */
    BeatDetect& input(const std::string& name) {
        AudioAnalyzer::input(name);
        return *this;
    }

    /**
     * @brief Set detection sensitivity
     * @param s Sensitivity multiplier (1.0=normal, 2.0=more sensitive)
     *
     * Higher values trigger on smaller transients.
     * Lower values only trigger on strong beats.
     */
    BeatDetect& sensitivity(float s) {
        m_sensitivity = std::max(0.5f, std::min(3.0f, s));
        return *this;
    }

    /**
     * @brief Set energy decay rate
     * @param d Decay per frame (0.9=fast decay, 0.99=slow decay)
     */
    BeatDetect& decay(float d) {
        m_decay = std::max(0.8f, std::min(0.999f, d));
        return *this;
    }

    /**
     * @brief Set minimum time between beats (debounce)
     * @param ms Minimum milliseconds between beat triggers
     */
    BeatDetect& holdTime(float ms) {
        m_holdTimeMs = std::max(0.0f, std::min(500.0f, ms));
        return *this;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Detection Results
    /// @{

    /**
     * @brief Check if beat occurred this frame
     * @return True on beat onset
     */
    bool beat() const { return m_beat; }

    /**
     * @brief Get current energy level (0-1)
     *
     * Smoothed energy that decays over time.
     * Useful for pulsing effects.
     */
    float energy() const { return m_energy; }

    /**
     * @brief Get raw instantaneous energy
     */
    float rawEnergy() const { return m_rawEnergy; }

    /**
     * @brief Get beat intensity (0-1)
     *
     * How strong the last beat was.
     * Decays after beat trigger.
     */
    float intensity() const { return m_intensity; }

    /**
     * @brief Get time since last beat in seconds
     */
    float timeSinceBeat() const { return m_timeSinceBeat; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "BeatDetect"; }

    /// @}

protected:
    void initAnalyzer(Context& ctx) override;
    void analyze(const float* input, uint32_t frames, uint32_t channels) override;

private:
    float m_sensitivity = 1.5f;
    float m_decay = 0.95f;
    float m_holdTimeMs = 100.0f;

    // Detection state
    bool m_beat = false;
    float m_energy = 0.0f;
    float m_rawEnergy = 0.0f;
    float m_intensity = 0.0f;
    float m_timeSinceBeat = 1.0f;

    // Energy history for adaptive threshold
    static constexpr int HISTORY_SIZE = 43;  // ~1 second at 43 fps
    std::vector<float> m_energyHistory;
    int m_historyPos = 0;
    float m_avgEnergy = 0.0f;

    // Timing
    float m_holdTimer = 0.0f;
    float m_lastFrameTime = 0.0f;
};

} // namespace vivid::audio
