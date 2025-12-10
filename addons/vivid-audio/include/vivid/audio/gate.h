#pragma once

/**
 * @file gate.h
 * @brief Noise gate
 *
 * Attenuates audio below a threshold level,
 * useful for removing background noise.
 */

#include <vivid/audio/audio_effect.h>
#include <vivid/audio/dsp/envelope.h>

namespace vivid::audio {

/**
 * @brief Noise gate
 *
 * Silences audio that falls below a threshold level.
 * Useful for removing background noise between phrases.
 *
 * @par Parameters
 * - `threshold(dB)` - Level below which audio is gated (-60 to 0)
 * - `attack(ms)` - Attack time (0.1-100ms)
 * - `hold(ms)` - Hold time before release (0-500ms)
 * - `release(ms)` - Release time (10-1000ms)
 * - `range(dB)` - Amount of attenuation when gated (0 to -inf)
 * - `mix(m)` - Dry/wet mix (0-1)
 *
 * @par Example
 * @code
 * chain.add<Gate>("gate")
 *     .input("audio")
 *     .threshold(-40)    // Gate below -40 dB
 *     .attack(1)         // Fast attack
 *     .hold(50)          // 50ms hold
 *     .release(100)      // 100ms release
 *     .range(-80);       // Reduce to -80 dB when gated
 * @endcode
 */
class Gate : public AudioEffect {
public:
    Gate() = default;
    ~Gate() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    Gate& threshold(float dB) {
        m_thresholdDb = std::max(-80.0f, std::min(0.0f, dB));
        m_thresholdLinear = dsp::EnvelopeFollower::dbToLinear(m_thresholdDb);
        return *this;
    }

    Gate& attack(float ms) {
        m_attackMs = std::max(0.1f, std::min(100.0f, ms));
        return *this;
    }

    Gate& hold(float ms) {
        m_holdMs = std::max(0.0f, std::min(500.0f, ms));
        return *this;
    }

    Gate& release(float ms) {
        m_releaseMs = std::max(10.0f, std::min(1000.0f, ms));
        return *this;
    }

    Gate& range(float dB) {
        m_rangeDb = std::max(-80.0f, std::min(0.0f, dB));
        m_rangeLinear = dsp::EnvelopeFollower::dbToLinear(m_rangeDb);
        return *this;
    }

    // Override base class methods to return Gate&
    Gate& input(const std::string& name) { AudioEffect::input(name); return *this; }
    Gate& mix(float amount) { AudioEffect::mix(amount); return *this; }
    Gate& bypass(bool b) { AudioEffect::bypass(b); return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name State Queries
    /// @{

    float getThreshold() const { return m_thresholdDb; }
    float getAttack() const { return m_attackMs; }
    float getHold() const { return m_holdMs; }
    float getRelease() const { return m_releaseMs; }
    float getRange() const { return m_rangeDb; }
    bool isOpen() const { return m_gateOpen; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "Gate"; }

    /// @}

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;
    void cleanupEffect() override;

private:
    // Parameters
    float m_thresholdDb = -40.0f;
    float m_thresholdLinear = 0.01f;
    float m_attackMs = 1.0f;
    float m_holdMs = 50.0f;
    float m_releaseMs = 100.0f;
    float m_rangeDb = -80.0f;
    float m_rangeLinear = 0.0001f;

    // State
    dsp::EnvelopeFollower m_envelope;
    float m_gateGain = 0.0f;  // Current gate gain (0 to 1)
    float m_holdCounter = 0.0f;
    bool m_gateOpen = false;
    uint32_t m_sampleRate = 48000;
};

} // namespace vivid::audio
