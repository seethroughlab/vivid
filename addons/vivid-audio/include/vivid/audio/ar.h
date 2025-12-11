#pragma once

/**
 * @file ar.h
 * @brief Attack-Release envelope
 *
 * Two-stage envelope without sustain - useful for plucks and percussion.
 */

#include <vivid/audio_operator.h>
#include <vivid/param.h>
#include <string>
#include <vector>

namespace vivid::audio {

/**
 * @brief AR envelope stage
 */
enum class ARStage {
    Idle,
    Attack,
    Release
};

/**
 * @brief Attack-Release envelope
 *
 * Simplified two-stage envelope. When triggered, ramps up during attack
 * then immediately decays during release. No sustain phase - the envelope
 * is one-shot. Useful for plucks, bells, and percussive tones.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | attack | float | 0.001-5 | 0.01 | Attack time in seconds |
 * | release | float | 0.001-10 | 0.3 | Release time in seconds |
 *
 * @par Example
 * @code
 * chain.add<Oscillator>("osc").frequency(880.0f).waveform(Waveform::Triangle);
 * chain.add<AR>("env")
 *     .input("osc")
 *     .attack(0.005f)
 *     .release(0.5f);
 *
 * chain.get<AR>("env")->trigger();
 * @endcode
 */
class AR : public AudioOperator {
public:
    AR() = default;
    ~AR() override = default;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set attack time
     * @param seconds Attack time (0.001-5 seconds)
     */
    AR& attack(float seconds) { m_attack = seconds; return *this; }

    /**
     * @brief Set release time
     * @param seconds Release time (0.001-10 seconds)
     */
    AR& release(float seconds) { m_release = seconds; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    /**
     * @brief Trigger the envelope
     */
    void trigger();

    /**
     * @brief Reset to idle
     */
    void reset();

    /**
     * @brief Check if active
     */
    bool isActive() const { return m_stage != ARStage::Idle; }

    /**
     * @brief Get current stage
     */
    ARStage stage() const { return m_stage; }

    /**
     * @brief Get current value
     */
    float currentValue() const { return m_value; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "AR"; }

    std::vector<ParamDecl> params() override {
        return { m_attack.decl(), m_release.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "attack") { out[0] = m_attack; return true; }
        if (name == "release") { out[0] = m_release; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "attack") { m_attack = value[0]; return true; }
        if (name == "release") { m_release = value[0]; return true; }
        return false;
    }

    /// @}

private:
    // Parameters
    Param<float> m_attack{"attack", 0.01f, 0.001f, 5.0f};
    Param<float> m_release{"release", 0.3f, 0.001f, 10.0f};

    // State
    ARStage m_stage = ARStage::Idle;
    float m_value = 0.0f;
    float m_progress = 0.0f;
    uint32_t m_sampleRate = 48000;

    bool m_initialized = false;
};

} // namespace vivid::audio
