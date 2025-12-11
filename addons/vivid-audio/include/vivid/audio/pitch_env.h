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
    PitchEnv() = default;
    ~PitchEnv() override = default;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set starting frequency
     * @param hz Start frequency in Hz
     */
    PitchEnv& startFreq(float hz) { m_startFreq = hz; return *this; }

    /**
     * @brief Set ending frequency
     * @param hz End frequency in Hz
     */
    PitchEnv& endFreq(float hz) { m_endFreq = hz; return *this; }

    /**
     * @brief Set sweep time
     * @param seconds Sweep duration
     */
    PitchEnv& time(float seconds) { m_time = seconds; return *this; }

    /// @}
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

    std::vector<ParamDecl> params() override {
        return { m_startFreq.decl(), m_endFreq.decl(), m_time.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "startFreq") { out[0] = m_startFreq; return true; }
        if (name == "endFreq") { out[0] = m_endFreq; return true; }
        if (name == "time") { out[0] = m_time; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "startFreq") { m_startFreq = value[0]; return true; }
        if (name == "endFreq") { m_endFreq = value[0]; return true; }
        if (name == "time") { m_time = value[0]; return true; }
        return false;
    }

    /// @}

private:
    // Parameters
    Param<float> m_startFreq{"startFreq", 150.0f, 20.0f, 2000.0f};
    Param<float> m_endFreq{"endFreq", 50.0f, 20.0f, 2000.0f};
    Param<float> m_time{"time", 0.1f, 0.001f, 2.0f};

    // State
    float m_currentFreq = 50.0f;
    float m_progress = 1.0f;
    uint32_t m_sampleRate = 48000;

    bool m_initialized = false;
};

} // namespace vivid::audio
