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
#include <vivid/param.h>
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
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> sensitivity{"sensitivity", 1.5f, 0.5f, 3.0f};   ///< Detection sensitivity
    Param<float> decay{"decay", 0.95f, 0.8f, 0.999f};           ///< Energy decay rate
    Param<float> holdTime{"holdTime", 100.0f, 0.0f, 500.0f};    ///< Debounce time in ms

    /// @}
    // -------------------------------------------------------------------------

    BeatDetect() {
        registerParam(sensitivity);
        registerParam(decay);
        registerParam(holdTime);
    }

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
