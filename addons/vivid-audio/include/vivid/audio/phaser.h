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
 * - `rate(Hz)` - LFO rate (0.05-5 Hz)
 * - `depth(f)` - Modulation depth (0-1)
 * - `stages(n)` - Number of all-pass stages (2-12, must be even)
 * - `feedback(f)` - Feedback amount (-1 to 1)
 * - `mix(m)` - Dry/wet mix (0-1)
 *
 * @par Example
 * @code
 * chain.add<Phaser>("phaser")
 *     .input("audio")
 *     .rate(0.3f)       // Moderate sweep rate
 *     .depth(0.8f)      // Deep modulation
 *     .stages(6)        // 6 stages (3 notches)
 *     .feedback(0.5f)   // Some feedback
 *     .mix(0.5f);
 * @endcode
 */
class Phaser : public AudioEffect {
public:
    Phaser() = default;
    ~Phaser() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    Phaser& rate(float hz) {
        m_rateHz = std::max(0.05f, std::min(5.0f, hz));
        m_lfoL.setFrequency(m_rateHz);
        m_lfoR.setFrequency(m_rateHz);
        return *this;
    }

    Phaser& depth(float d) {
        m_depth = std::max(0.0f, std::min(1.0f, d));
        return *this;
    }

    Phaser& stages(int n) {
        // Round to nearest even number
        m_stages = std::max(2, std::min(12, (n / 2) * 2));
        return *this;
    }

    Phaser& feedback(float f) {
        m_feedback = std::max(-0.95f, std::min(0.95f, f));
        return *this;
    }

    // Override base class methods to return Phaser&
    Phaser& input(const std::string& name) { AudioEffect::input(name); return *this; }
    Phaser& mix(float amount) { AudioEffect::mix(amount); return *this; }
    Phaser& bypass(bool b) { AudioEffect::bypass(b); return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name State Queries
    /// @{

    float getRate() const { return m_rateHz; }
    float getDepth() const { return m_depth; }
    int getStages() const { return m_stages; }
    float getFeedback() const { return m_feedback; }

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

private:
    // Parameters
    float m_rateHz = 0.3f;
    float m_depth = 0.8f;
    int m_stages = 6;
    float m_feedback = 0.5f;

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
