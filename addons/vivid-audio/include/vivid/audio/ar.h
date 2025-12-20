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
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> attack{"attack", 0.01f, 0.001f, 5.0f};    ///< Attack time in seconds
    Param<float> release{"release", 0.3f, 0.001f, 10.0f};  ///< Release time in seconds

    /// @}
    // -------------------------------------------------------------------------

    AR() {
        registerParam(attack);
        registerParam(release);
    }
    ~AR() override = default;

    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    void trigger();
    void reset();
    bool isActive() const { return m_stage != ARStage::Idle; }
    ARStage stage() const { return m_stage; }
    float currentValue() const { return m_value; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "AR"; }

    /// @}

private:

    // State
    ARStage m_stage = ARStage::Idle;
    float m_value = 0.0f;
    float m_progress = 0.0f;
    uint32_t m_sampleRate = 48000;
};

} // namespace vivid::audio
