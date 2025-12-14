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
#include <vivid/param.h>

namespace vivid::audio {

/**
 * @brief Delay effect with feedback
 *
 * Creates a delayed copy of the input signal that can feed
 * back into itself for repeating echoes.
 *
 * @par Parameters
 * - `delayTime` - Delay time in milliseconds (0-2000ms)
 * - `feedback` - Feedback amount (0-1, 0=single echo, 0.9=long decay)
 * - `mix` - Dry/wet mix (0=dry, 1=wet)
 *
 * @par Example
 * @code
 * chain.add<Delay>("delay").input("audio");
 * auto* delay = chain.get<Delay>("delay");
 * delay->delayTime = 250.0f;  // 250ms (quarter note at 120 BPM)
 * delay->feedback = 0.4f;     // Moderate feedback
 * delay->mix = 0.3f;          // 30% wet
 * @endcode
 */
class Delay : public AudioEffect {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> delayTime{"delayTime", 250.0f, 0.0f, 2000.0f};  ///< Delay time in ms
    Param<float> feedback{"feedback", 0.3f, 0.0f, 0.99f};         ///< Feedback amount
    Param<float> mix{"mix", 0.5f, 0.0f, 1.0f};                    ///< Dry/wet mix

    /// @}
    // -------------------------------------------------------------------------

    Delay() {
        registerParam(delayTime);
        registerParam(feedback);
        registerParam(mix);
    }
    ~Delay() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    // Override base class methods to return Delay&
    Delay& input(const std::string& name) {
        AudioEffect::input(name);
        return *this;
    }
    Delay& bypass(bool b) {
        AudioEffect::bypass(b);
        return *this;
    }

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
