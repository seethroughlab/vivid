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
#include <vivid/operator_registry.h>
#include <vivid/param.h>

namespace vivid::audio {

/**
 * @brief Flanger effect
 *
 * Creates a sweeping, jet-like sound by mixing the original
 * with a short, modulated delay with feedback.
 *
 * @par Parameters
 * - `rate` - LFO rate (0.05-5 Hz)
 * - `depth` - Modulation depth (0-1)
 * - `feedback` - Feedback amount (-0.95 to 0.95)
 * - `mix` - Dry/wet mix (0-1)
 *
 * @par Example
 * @code
 * chain.add<Flanger>("flanger").input("audio");
 * auto* flanger = chain.get<Flanger>("flanger");
 * flanger->rate = 0.2f;       // Slow sweep
 * flanger->depth = 0.7f;      // Deep modulation
 * flanger->feedback = 0.5f;   // Moderate feedback
 * flanger->mix = 0.5f;
 * @endcode
 
 * @see Chorus, Phaser, Delay
 */
class Flanger : public AudioEffect {
public:
    // -------------------------------------------------------------------------
    /// @name Self-Description
    /// @{

    static OperatorDescriptor describe() {
        return OperatorDescriptor("Flanger", "Audio Effects", "Flanger effect with LFO modulation")
            .output(OutputKind::Audio)
            .requireInput()
            .inModule("vivid-audio")
            .withUsage(
                "auto& flanger = chain.add<Flanger>(\"flanger\");\n"
                "flanger.input(\"audio\");\n"
                "flanger.rate = 0.2f;      // Slow sweep\n"
                "flanger.depth = 0.7f;     // Deep modulation\n"
                "flanger.feedback = 0.5f;  // Moderate feedback\n"
                "flanger.mix = 0.5f;\n"
            )
            .withExamples({{"modules/vivid-audio/examples/audio-effects"}});
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> rate{"rate", 0.2f, 0.05f, 5.0f};         ///< LFO rate in Hz
    Param<float> depth{"depth", 0.7f, 0.0f, 1.0f};        ///< Modulation depth
    Param<float> feedback{"feedback", 0.5f, -0.95f, 0.95f}; ///< Feedback amount
    Param<float> mix{"mix", 0.5f, 0.0f, 1.0f};            ///< Dry/wet mix

    /// @}
    // -------------------------------------------------------------------------

    Flanger() {
        registerParam(rate);
        registerParam(depth);
        registerParam(feedback);
        registerParam(mix);
    }
    ~Flanger() override = default;

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "Flanger"; }

    /// @}

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;
    void cleanupEffect() override;

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
