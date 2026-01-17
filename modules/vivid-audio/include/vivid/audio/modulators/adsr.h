#pragma once

/**
 * @file adsr.h
 * @brief ADSR envelope modulator
 *
 * ADSR generates attack-decay-sustain-release envelope curves for modulation.
 * Can be used standalone as an operator or attached to synths for per-voice modulation.
 */

#include <vivid/audio/modulator.h>
#include <vivid/audio/envelope.h>
#include <vivid/operator_registry.h>
#include <cmath>

namespace vivid::audio {

/**
 * @brief Per-voice ADSR state
 */
class ADSRState : public ModulatorState {
public:
    EnvelopeStage stage = EnvelopeStage::Idle;  ///< Current envelope stage
    float value = 0.0f;                          ///< Current envelope value [0, 1]
    float progress = 0.0f;                       ///< Progress through current stage [0, 1]
    float releaseStartValue = 0.0f;              ///< Value when release started

    void reset() override {
        stage = EnvelopeStage::Idle;
        value = 0.0f;
        progress = 0.0f;
        releaseStartValue = 0.0f;
    }
};

/**
 * @brief ADSR envelope modulator
 *
 * Generates Attack-Decay-Sustain-Release envelope curves for modulating
 * parameters. Typically used for filter envelopes in synthesizers.
 *
 * @par Dual-Use Design
 * - **Standalone**: Add to chain with `chain.add<ADSRMod>()` for gate-triggered envelopes
 * - **Attached**: Add to synth with `synth.addModulator<ADSRMod>()` for per-voice
 *
 * @par Output Range
 * By default outputs 0 to 1 (unipolar). When used with modulate(), the depth
 * parameter controls how much of the envelope is applied.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | attack | float | 0.001-10 | 0.01 | Attack time in seconds |
 * | decay | float | 0.001-10 | 0.1 | Decay time in seconds |
 * | sustain | float | 0-1 | 0.7 | Sustain level |
 * | release | float | 0.001-10 | 0.3 | Release time in seconds |
 *
 * @par Example
 * @code
 * // Per-voice filter envelope inside synth
 * auto& filterEnv = synth.addModulator<ADSRMod>("filterEnv");
 * filterEnv.attack = 0.01f;
 * filterEnv.decay = 0.3f;
 * filterEnv.sustain = 0.2f;
 * filterEnv.release = 0.5f;
 * filterEnv.perVoice = true;  // Always true for envelopes
 * synth.modulate(filterEnv, "filterCutoff", 0.8f, false);  // Unipolar
 * @endcode
 *
 * @see LFO, Envelope, WavetableSynth
 */
class ADSRMod : public Modulator, public AudioOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters
    /// @{

    Param<float> attack{"attack", 0.01f, 0.001f, 10.0f};   ///< Attack time in seconds
    Param<float> decay{"decay", 0.1f, 0.001f, 10.0f};      ///< Decay time in seconds
    Param<float> sustain{"sustain", 0.7f, 0.0f, 1.0f};     ///< Sustain level
    Param<float> release{"release", 0.3f, 0.001f, 10.0f};  ///< Release time in seconds

    /// @}
    // -------------------------------------------------------------------------

    ADSRMod();
    ~ADSRMod() override = default;

    // -------------------------------------------------------------------------
    /// @name State Queries
    /// @{

    /**
     * @brief Get current envelope value (for standalone mode)
     * @return Current value [0, 1]
     */
    float value() const { return m_globalState.value; }

    /**
     * @brief Check if envelope is active
     */
    bool isActive() const { return m_globalState.stage != EnvelopeStage::Idle; }

    /**
     * @brief Get current stage
     */
    EnvelopeStage stage() const { return m_globalState.stage; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Trigger Control (standalone mode)
    /// @{

    /**
     * @brief Trigger the envelope (start attack phase)
     */
    void trigger();

    /**
     * @brief Release the envelope (start release phase)
     */
    void releaseNote();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Modulator Interface
    /// @{

    std::string modulatorName() const override { return "ADSR"; }

    std::unique_ptr<ModulatorState> createState() const override {
        return std::make_unique<ADSRState>();
    }

    float process(ModulatorState& state, float sampleRate) override;

    void noteOn(ModulatorState& state) override;

    void noteOff(ModulatorState& state) override;

    bool isActive(const ModulatorState& state) const override {
        const ADSRState& s = static_cast<const ADSRState&>(state);
        return s.stage != EnvelopeStage::Idle;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name AudioOperator Interface (for standalone use)
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "ADSRMod"; }

    void generateBlock(uint32_t frameCount) override;
    void handleEvent(const AudioEvent& event) override;

    /// @}

private:
    float computeEnvelopeValue(const ADSRState& state) const;
    void advanceEnvelope(ADSRState& state, float sampleRate);

    ADSRState m_globalState;
    uint32_t m_sampleRate = 48000;
};

} // namespace vivid::audio
