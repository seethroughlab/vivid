#pragma once

/**
 * @file lfo.h
 * @brief Low Frequency Oscillator modulator
 *
 * LFO generates periodic modulation signals for controlling parameters.
 * Can be used standalone as an operator or attached to synths for per-voice modulation.
 */

#include <vivid/audio/modulator.h>
#include <vivid/audio/clock.h>
#include <vivid/audio/glitch/rate_utils.h>
#include <vivid/operator_registry.h>
#include <cmath>

namespace vivid::audio {

/**
 * @brief LFO waveform types
 */
enum class LFOWaveform {
    Sine,       ///< Smooth sine wave
    Triangle,   ///< Linear triangle wave
    Square,     ///< Square wave (on/off)
    Saw,        ///< Rising sawtooth
    SawDown,    ///< Falling sawtooth
    SampleHold  ///< Random sample-and-hold
};

/**
 * @brief Per-voice LFO state
 */
class LFOState : public ModulatorState {
public:
    float phase = 0.0f;         ///< Current phase [0, 1)
    float value = 0.0f;         ///< Current output value [-1, 1]
    float sampleHoldValue = 0.0f; ///< Held value for S&H mode
    bool triggered = false;     ///< Did phase wrap this sample?

    void reset() override {
        phase = 0.0f;
        value = 0.0f;
        triggered = false;
        // Note: sampleHoldValue intentionally not reset for smooth continuation
    }
};

/**
 * @brief Low Frequency Oscillator for modulation
 *
 * Generates periodic waveforms for modulating parameters like filter cutoff,
 * volume (tremolo), pitch (vibrato), or wavetable position.
 *
 * @par Dual-Use Design
 * - **Standalone**: Add to chain with `chain.add<LFO>()` for global modulation
 * - **Attached**: Add to synth with `synth.addModulator<LFO>()` for per-voice
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | rate | float | 0.01-100 | 1.0 | Rate in Hz (free mode) |
 * | waveform | enum | 0-5 | 0 | Waveform type |
 * | sync | bool | 0-1 | 0 | Tempo sync mode |
 * | division | int | 0-9 | 4 | Clock division when synced |
 * | phase | float | 0-1 | 0 | Starting phase offset |
 *
 * @par Example
 * @code
 * // Standalone LFO for global effect modulation
 * auto& lfo = chain.add<LFO>("globalLfo");
 * lfo.rate = 0.5f;  // Slow sweep
 * lfo.waveform = LFOWaveform::Sine;
 *
 * // Per-voice LFO inside synth
 * auto& vibrato = synth.addModulator<LFO>("vibrato");
 * vibrato.rate = 5.0f;
 * vibrato.perVoice = true;
 * synth.modulate(vibrato, "position", 0.3f);
 * @endcode
 *
 * @see ADSR, WavetableSynth, Clock
 */
class LFO : public Modulator, public AudioOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters
    /// @{

    Param<float> rate{"rate", 1.0f, 0.01f, 100.0f};     ///< Rate in Hz (free mode)
    EnumParam<LFOWaveform> waveform{"waveform", LFOWaveform::Sine}; ///< Waveform type
    Param<bool> sync{"sync", false};                     ///< Tempo sync mode
    Param<int> division{"division", 4, 0, 9};            ///< ClockDiv index for sync
    Param<float> startPhase{"startPhase", 0.0f, 0.0f, 1.0f}; ///< Starting phase offset
    Param<float> bpm{"bpm", 120.0f, 20.0f, 300.0f};      ///< Internal BPM for tempo sync

    /// @}
    // -------------------------------------------------------------------------

    LFO();
    ~LFO() override = default;

    // -------------------------------------------------------------------------
    /// @name Clock Source
    /// @{

    /**
     * @brief Set external clock source for tempo sync
     * @param clockName Name of Clock operator in chain (uses its BPM)
     */
    void setClockSource(const std::string& clockName) { m_clockSourceName = clockName; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name State Queries
    /// @{

    /**
     * @brief Get current LFO value (for standalone mode)
     * @return Current value [-1, 1]
     *
     * Use in update() for parameter automation:
     * @code
     * filter.cutoff = 1000.0f + 500.0f * lfo.value();
     * @endcode
     */
    float value() const { return m_globalState.value; }

    /**
     * @brief Check if LFO phase wrapped this sample
     */
    bool triggered() const override { return m_globalState.triggered; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Modulator Interface
    /// @{

    std::string modulatorName() const override { return "LFO"; }

    std::unique_ptr<ModulatorState> createState() const override {
        auto state = std::make_unique<LFOState>();
        state->phase = static_cast<float>(startPhase);
        return state;
    }

    float process(ModulatorState& state, float sampleRate) override;

    void noteOn(ModulatorState& state) override;

    bool isActive(const ModulatorState& state) const override { return true; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name AudioOperator Interface (for standalone use)
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "LFO"; }

    void generateBlock(uint32_t frameCount) override;

    /// @}

private:
    float calculateFrequency() const;
    float generateSample(float phase) const;

    LFOState m_globalState;
    std::string m_clockSourceName;
    Clock* m_cachedClock = nullptr;
    uint32_t m_sampleRate = 48000;

    // For S&H mode: simple PRNG
    uint32_t m_randSeed = 12345;
    float nextRandom() {
        m_randSeed = m_randSeed * 1103515245 + 12345;
        return (static_cast<float>(m_randSeed & 0x7FFFFFFF) / static_cast<float>(0x7FFFFFFF)) * 2.0f - 1.0f;
    }

    static constexpr float TWO_PI = 6.28318530717958647692f;
};

} // namespace vivid::audio
