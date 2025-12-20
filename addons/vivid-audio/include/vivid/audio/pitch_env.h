#pragma once

/**
 * @file pitch_env.h
 * @brief Pitch envelope for drum synthesis
 *
 * Sweeps pitch from start to end frequency - essential for kick drums and toms.
 */

#include <vivid/audio_operator.h>
#include <vivid/param.h>
#include <string>
#include <vector>

namespace vivid::audio {

/**
 * @brief Pitch envelope for frequency sweeps
 *
 * Generates a frequency sweep from start to end frequency over time.
 * Essential for kick drums (pitch drops from ~150Hz to ~50Hz) and
 * toms. Can be used to modulate oscillator frequency.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | startFreq | float | 20-2000 | 150 | Starting frequency in Hz |
 * | endFreq | float | 20-2000 | 50 | Ending frequency in Hz |
 * | time | float | 0.001-2 | 0.1 | Sweep time in seconds |
 *
 * @par Example
 * @code
 * // Kick drum pitch envelope
 * chain.add<PitchEnv>("pitchEnv")
 *     .startFreq(150.0f)
 *     .endFreq(50.0f)
 *     .time(0.1f);
 *
 * // Use to modulate oscillator
 * float freq = chain.get<PitchEnv>("pitchEnv")->currentFreq();
 * chain.get<Oscillator>("osc")->frequency(freq);
 * @endcode
 */
class PitchEnv : public AudioOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> startFreq{"startFreq", 150.0f, 20.0f, 2000.0f};  ///< Starting frequency in Hz
    Param<float> endFreq{"endFreq", 50.0f, 20.0f, 2000.0f};       ///< Ending frequency in Hz
    Param<float> time{"time", 0.1f, 0.001f, 2.0f};                ///< Sweep time in seconds

    /// @}
    // -------------------------------------------------------------------------

    PitchEnv() {
        registerParam(startFreq);
        registerParam(endFreq);
        registerParam(time);
    }
    ~PitchEnv() override = default;

    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    /**
     * @brief Trigger the pitch sweep
     */
    void trigger();

    /**
     * @brief Reset to idle
     */
    void reset();

    /**
     * @brief Get current frequency value
     */
    float currentFreq() const { return m_currentFreq; }

    /**
     * @brief Check if sweep is active
     */
    bool isActive() const { return m_progress < 1.0f; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "PitchEnv"; }

    /// @}

private:
    // State
    float m_currentFreq = 50.0f;
    float m_progress = 1.0f;
    uint32_t m_sampleRate = 48000;
};

} // namespace vivid::audio
