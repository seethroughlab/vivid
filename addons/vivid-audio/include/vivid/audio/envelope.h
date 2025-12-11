#pragma once

/**
 * @file envelope.h
 * @brief ADSR Envelope generator for amplitude shaping
 *
 * Generates envelope curves to modulate audio amplitude over time.
 */

#include <vivid/audio_operator.h>
#include <vivid/param.h>
#include <string>
#include <vector>

namespace vivid::audio {

/**
 * @brief Envelope stage
 */
enum class EnvelopeStage {
    Idle,       ///< Not triggered, output 0
    Attack,     ///< Rising from 0 to 1
    Decay,      ///< Falling from 1 to sustain level
    Sustain,    ///< Holding at sustain level
    Release     ///< Falling from sustain to 0
};

/**
 * @brief ADSR envelope generator
 *
 * Applies an ADSR (Attack-Decay-Sustain-Release) envelope to an audio input.
 * When triggered, the envelope ramps up during attack, drops to sustain level
 * during decay, holds during sustain, and fades out during release.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | attack | float | 0.001-5 | 0.01 | Attack time in seconds |
 * | decay | float | 0.001-5 | 0.1 | Decay time in seconds |
 * | sustain | float | 0-1 | 0.7 | Sustain level |
 * | release | float | 0.001-10 | 0.3 | Release time in seconds |
 *
 * @par Example
 * @code
 * chain.add<Oscillator>("osc").frequency(440.0f).waveform(Waveform::Saw);
 * chain.add<Envelope>("env")
 *     .input("osc")
 *     .attack(0.01f)
 *     .decay(0.2f)
 *     .sustain(0.5f)
 *     .release(0.5f);
 *
 * // Trigger the envelope
 * chain.get<Envelope>("env")->trigger();
 * @endcode
 */
class Envelope : public AudioOperator {
public:
    Envelope() = default;
    ~Envelope() override = default;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set attack time
     * @param seconds Attack time (0.001-5 seconds)
     */
    Envelope& attack(float seconds) { m_attack = seconds; return *this; }

    /**
     * @brief Set decay time
     * @param seconds Decay time (0.001-5 seconds)
     */
    Envelope& decay(float seconds) { m_decay = seconds; return *this; }

    /**
     * @brief Set sustain level
     * @param level Sustain level (0-1)
     */
    Envelope& sustain(float level) { m_sustain = level; return *this; }

    /**
     * @brief Set release time
     * @param seconds Release time (0.001-10 seconds)
     */
    Envelope& release(float seconds) { m_release = seconds; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    /**
     * @brief Trigger the envelope (start attack phase)
     */
    void trigger();

    /**
     * @brief Release the envelope (start release phase)
     */
    void releaseNote();

    /**
     * @brief Reset envelope to idle state
     */
    void reset();

    /**
     * @brief Check if envelope is active (not idle)
     */
    bool isActive() const { return m_stage != EnvelopeStage::Idle; }

    /**
     * @brief Get current envelope stage
     */
    EnvelopeStage stage() const { return m_stage; }

    /**
     * @brief Get current envelope value (0-1)
     */
    float currentValue() const { return m_currentValue; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Envelope"; }

    std::vector<ParamDecl> params() override {
        return { m_attack.decl(), m_decay.decl(), m_sustain.decl(), m_release.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "attack") { out[0] = m_attack; return true; }
        if (name == "decay") { out[0] = m_decay; return true; }
        if (name == "sustain") { out[0] = m_sustain; return true; }
        if (name == "release") { out[0] = m_release; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "attack") { m_attack = value[0]; return true; }
        if (name == "decay") { m_decay = value[0]; return true; }
        if (name == "sustain") { m_sustain = value[0]; return true; }
        if (name == "release") { m_release = value[0]; return true; }
        return false;
    }

    /// @}

private:
    float computeEnvelopeValue();
    void advanceEnvelope(uint32_t samples);

    // Parameters
    Param<float> m_attack{"attack", 0.01f, 0.001f, 5.0f};
    Param<float> m_decay{"decay", 0.1f, 0.001f, 5.0f};
    Param<float> m_sustain{"sustain", 0.7f, 0.0f, 1.0f};
    Param<float> m_release{"release", 0.3f, 0.001f, 10.0f};

    // State
    EnvelopeStage m_stage = EnvelopeStage::Idle;
    float m_currentValue = 0.0f;
    float m_stageProgress = 0.0f;  // 0-1 progress through current stage
    float m_releaseStartValue = 0.0f;  // Value when release started
    uint32_t m_sampleRate = 48000;

    bool m_initialized = false;
};

} // namespace vivid::audio
