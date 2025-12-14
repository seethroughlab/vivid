#pragma once

/**
 * @file phaser.h
 * @brief Phaser effect
 *
 * Creates a sweeping, swirling sound using
 * cascaded all-pass filters with LFO modulation.
 */

#include <vivid/audio/audio_effect.h>
#include <vivid/audio/dsp/filters.h>
#include <vivid/audio/dsp/lfo.h>
#include <vivid/param.h>
#include <array>

namespace vivid::audio {

/**
 * @brief Phaser effect
 *
 * Creates a sweeping, swirling sound by mixing the original
 * with a phase-shifted copy. The phase shift is created by
 * cascaded all-pass filters whose cutoff is modulated by an LFO.
 *
 * @par Parameters
 * - `rate` - LFO rate (0.05-5 Hz)
 * - `depth` - Modulation depth (0-1)
 * - `stages` - Number of all-pass stages (2-12)
 * - `feedback` - Feedback amount (-0.95 to 0.95)
 * - `mix` - Dry/wet mix (0-1)
 *
 * @par Example
 * @code
 * chain.add<Phaser>("phaser").input("audio");
 * auto* phaser = chain.get<Phaser>("phaser");
 * phaser->rate = 0.3f;       // Moderate sweep rate
 * phaser->depth = 0.8f;      // Deep modulation
 * phaser->stages = 6;        // 6 stages (3 notches)
 * phaser->feedback = 0.5f;   // Some feedback
 * phaser->mix = 0.5f;
 * @endcode
 */
class Phaser : public AudioEffect {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> rate{"rate", 0.3f, 0.05f, 5.0f};         ///< LFO rate in Hz
    Param<float> depth{"depth", 0.8f, 0.0f, 1.0f};        ///< Modulation depth
    Param<int> stages{"stages", 6, 2, 12};                ///< Number of all-pass stages
    Param<float> feedback{"feedback", 0.5f, -0.95f, 0.95f}; ///< Feedback amount
    Param<float> mix{"mix", 0.5f, 0.0f, 1.0f};            ///< Dry/wet mix

    /// @}
    // -------------------------------------------------------------------------

    Phaser() {
        registerParam(rate);
        registerParam(depth);
        registerParam(stages);
        registerParam(feedback);
        registerParam(mix);
    }
    ~Phaser() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    // Override base class methods to return Phaser&
    Phaser& input(const std::string& name) { AudioEffect::input(name); return *this; }
    Phaser& bypass(bool b) { AudioEffect::bypass(b); return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "Phaser"; }

    /// @}

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;
    void cleanupEffect() override;

    // DSP - max 12 stages
    static constexpr int MAX_STAGES = 12;
    std::array<dsp::AllPassFilter, MAX_STAGES> m_allpassL;
    std::array<dsp::AllPassFilter, MAX_STAGES> m_allpassR;
    dsp::LFO m_lfoL;
    dsp::LFO m_lfoR;
    float m_feedbackL = 0.0f;
    float m_feedbackR = 0.0f;
    uint32_t m_sampleRate = 48000;

    // Frequency range for modulation (Hz)
    static constexpr float MIN_FREQ = 200.0f;
    static constexpr float MAX_FREQ = 4000.0f;
};

} // namespace vivid::audio
