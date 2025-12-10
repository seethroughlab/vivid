#pragma once

/**
 * @file flanger.h
 * @brief Flanger effect
 *
 * Creates a sweeping, jet-like sound using a short
 * modulated delay with feedback.
 */

#include <vivid/audio/audio_effect.h>
#include <vivid/audio/dsp/delay_line.h>
#include <vivid/audio/dsp/lfo.h>

namespace vivid::audio {

/**
 * @brief Flanger effect
 *
 * Creates a sweeping, jet-like sound by mixing the original
 * with a short, modulated delay with feedback.
 *
 * @par Parameters
 * - `rate(Hz)` - LFO rate (0.05-5 Hz)
 * - `depth(f)` - Modulation depth (0-1)
 * - `feedback(f)` - Feedback amount (-1 to 1, negative = inverted)
 * - `mix(m)` - Dry/wet mix (0-1)
 *
 * @par Example
 * @code
 * chain.add<Flanger>("flanger")
 *     .input("audio")
 *     .rate(0.2f)       // Slow sweep
 *     .depth(0.7f)      // Deep modulation
 *     .feedback(0.5f)   // Moderate feedback
 *     .mix(0.5f);
 * @endcode
 */
class Flanger : public AudioEffect {
public:
    Flanger() = default;
    ~Flanger() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    Flanger& rate(float hz) {
        m_rateHz = std::max(0.05f, std::min(5.0f, hz));
        m_lfoL.setFrequency(m_rateHz);
        m_lfoR.setFrequency(m_rateHz);
        return *this;
    }

    Flanger& depth(float d) {
        m_depth = std::max(0.0f, std::min(1.0f, d));
        return *this;
    }

    Flanger& feedback(float f) {
        m_feedback = std::max(-0.95f, std::min(0.95f, f));
        return *this;
    }

    // Override base class methods to return Flanger&
    Flanger& input(const std::string& name) { AudioEffect::input(name); return *this; }
    Flanger& mix(float amount) { AudioEffect::mix(amount); return *this; }
    Flanger& bypass(bool b) { AudioEffect::bypass(b); return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name State Queries
    /// @{

    float getRate() const { return m_rateHz; }
    float getDepth() const { return m_depth; }
    float getFeedback() const { return m_feedback; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "Flanger"; }

    /// @}

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;
    void cleanupEffect() override;

private:
    // Parameters
    float m_rateHz = 0.2f;
    float m_depth = 0.7f;
    float m_feedback = 0.5f;

    // DSP
    dsp::DelayLine m_delayL;
    dsp::DelayLine m_delayR;
    dsp::LFO m_lfoL;
    dsp::LFO m_lfoR;
    float m_feedbackL = 0.0f;
    float m_feedbackR = 0.0f;
    uint32_t m_sampleRate = 48000;

    // Flanger delay range: 0.1 to 10ms
    static constexpr float MIN_DELAY_MS = 0.1f;
    static constexpr float MAX_DELAY_MS = 10.0f;
};

} // namespace vivid::audio
