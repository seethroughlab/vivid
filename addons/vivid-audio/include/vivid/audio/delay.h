#pragma once

/**
 * @file delay.h
 * @brief Delay audio effect
 *
 * Simple delay with feedback control. The delayed signal
 * feeds back into the delay line for repeating echoes.
 */

#include <vivid/audio/audio_effect.h>
#include <vivid/audio/dsp/delay_line.h>

namespace vivid::audio {

/**
 * @brief Delay effect with feedback
 *
 * Creates a delayed copy of the input signal that can feed
 * back into itself for repeating echoes.
 *
 * @par Parameters
 * - `delayTime(ms)` - Delay time in milliseconds (0-2000ms)
 * - `feedback(f)` - Feedback amount (0-1, 0=single echo, 0.9=long decay)
 * - `mix(m)` - Dry/wet mix (0=dry, 1=wet)
 *
 * @par Example
 * @code
 * chain.add<Delay>("delay")
 *     .input("audio")
 *     .delayTime(250)    // 250ms delay (quarter note at 120 BPM)
 *     .feedback(0.4f)    // Moderate feedback
 *     .mix(0.3f);        // 30% wet
 * @endcode
 */
class Delay : public AudioEffect {
public:
    Delay() = default;
    ~Delay() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Set delay time in milliseconds
     * @param ms Delay time (0-2000ms)
     * @return Reference for chaining
     */
    Delay& delayTime(float ms) {
        m_delayTimeMs = std::max(0.0f, std::min(2000.0f, ms));
        updateDelaySamples();
        return *this;
    }

    /**
     * @brief Set feedback amount
     * @param f Feedback (0-1)
     * @return Reference for chaining
     */
    Delay& feedback(float f) {
        m_feedback = std::max(0.0f, std::min(0.99f, f));
        return *this;
    }

    // Override base class methods to return Delay&
    Delay& input(const std::string& name) {
        AudioEffect::input(name);
        return *this;
    }
    Delay& mix(float amount) {
        AudioEffect::mix(amount);
        return *this;
    }
    Delay& bypass(bool b) {
        AudioEffect::bypass(b);
        return *this;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name State Queries
    /// @{

    float getDelayTime() const { return m_delayTimeMs; }
    float getFeedback() const { return m_feedback; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "Delay"; }

    /// @}

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;
    void cleanupEffect() override;

private:
    void updateDelaySamples();

    // Parameters
    float m_delayTimeMs = 250.0f;  // Default 250ms
    float m_feedback = 0.3f;       // Default moderate feedback

    // DSP
    dsp::StereoDelayLine m_delayLine;
    uint32_t m_sampleRate = 48000;
    uint32_t m_delaySamples = 0;

    // DC blocking state for feedback path
    float m_prevDelayL = 0.0f;
    float m_prevDelayR = 0.0f;
    float m_dcBlockL = 0.0f;
    float m_dcBlockR = 0.0f;
};

} // namespace vivid::audio
