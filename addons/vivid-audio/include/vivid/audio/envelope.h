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
 * chain.add<Oscillator>("osc").waveform(Waveform::Saw);
 * chain.get<Oscillator>("osc")->frequency = 440.0f;
 * chain.add<Envelope>("env");
 * auto* env = chain.get<Envelope>("env");
 * env->attack = 0.01f;
 * env->decay = 0.2f;
 * env->sustain = 0.5f;
 * env->release = 0.5f;
 *
 * // Trigger the envelope
 * env->trigger();
 * @endcode
 */
class Envelope : public AudioOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> attack{"attack", 0.01f, 0.001f, 5.0f};    ///< Attack time in seconds
    Param<float> decay{"decay", 0.1f, 0.001f, 5.0f};       ///< Decay time in seconds
    Param<float> sustain{"sustain", 0.7f, 0.0f, 1.0f};     ///< Sustain level
    Param<float> release{"release", 0.3f, 0.001f, 10.0f};  ///< Release time in seconds

    /// @}
    // -------------------------------------------------------------------------

    Envelope() {
        registerParam(attack);
        registerParam(decay);
        registerParam(sustain);
        registerParam(release);
    }
    ~Envelope() override = default;
    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    // trigger() inherited from AudioOperator - queues to audio thread

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
    void generateBlock(uint32_t frameCount) override;
    void handleEvent(const AudioEvent& event) override;
    void cleanup() override;
    std::string name() const override { return "Envelope"; }

    /// @}

protected:
    void onTrigger() override;  // Called from audio thread

private:
    float computeEnvelopeValue();
    void advanceEnvelope(uint32_t samples);

    // State
    EnvelopeStage m_stage = EnvelopeStage::Idle;
    float m_currentValue = 0.0f;
    float m_stageProgress = 0.0f;  // 0-1 progress through current stage
    float m_releaseStartValue = 0.0f;  // Value when release started
    uint32_t m_sampleRate = 48000;
};

} // namespace vivid::audio
