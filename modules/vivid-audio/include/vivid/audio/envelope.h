#pragma once

/**
 * @file envelope.h
 * @brief ADSR Envelope generator for amplitude shaping
 *
 * Generates envelope curves to modulate audio amplitude over time.
 */

#include <vivid/audio_operator.h>
#include <vivid/operator_registry.h>
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
 
 * @see AR, Decay, Synth, Oscillator, AudioGain
 */
class Envelope : public AudioOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Self-Description
    /// @{

    static OperatorDescriptor describe() {
        return OperatorDescriptor("Envelope", "Audio Envelope", "ADSR envelope generator")
            .output(OutputKind::Audio)
            .withAliases({"ADSR", "Env"})
            .withUsage(
                "auto& env = chain.add<Envelope>(\"env\");\n"
                "env.input(\"osc\");\n"
                "env.attack = 0.01f;    // 10ms attack\n"
                "env.decay = 0.2f;      // 200ms decay\n"
                "env.sustain = 0.5f;    // 50% sustain level\n"
                "env.release = 0.5f;    // 500ms release\n"
                "\n"
                "env.trigger();       // Note on\n"
                "env.releaseNote();   // Note off\n"
                "\n"
                "// See: modules/vivid-audio/examples/envelope-modulation/\n"
            )
            .withExamples({{"examples/envelope-modulation"}});
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    ADSRParam envelope{"envelope", 0.01f, 0.1f, 0.7f, 0.3f, 5.0f};  ///< ADSR envelope parameters

    /// Convenience accessors (read/write individual ADSR values)
    float& attack;    ///< Attack time in seconds
    float& decay;     ///< Decay time in seconds
    float& sustain;   ///< Sustain level
    float& release;   ///< Release time in seconds

    /// @}
    // -------------------------------------------------------------------------

    Envelope()
        : attack(envelope.attackRef())
        , decay(envelope.decayRef())
        , sustain(envelope.sustainRef())
        , release(envelope.releaseRef())
    {
        registerParam(envelope);
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
