#pragma once

/**
 * @file chorus.h
 * @brief Chorus effect
 *
 * Creates a thicker, wider sound by mixing the original
 * with pitch-modulated copies.
 */

#include <vivid/audio/audio_effect.h>
#include <vivid/audio/dsp/delay_line.h>
#include <vivid/audio/dsp/lfo.h>

namespace vivid::audio {

/**
 * @brief Chorus effect
 *
 * Creates a fuller, wider sound by mixing the original
 * with delayed, pitch-modulated copies.
 *
 * @par Parameters
 * - `rate(Hz)` - LFO rate (0.1-10 Hz)
 * - `depth(ms)` - Modulation depth (0-20ms)
 * - `voices(n)` - Number of chorus voices (1-4)
 * - `mix(m)` - Dry/wet mix (0-1)
 *
 * @par Example
 * @code
 * chain.add<Chorus>("chorus")
 *     .input("audio")
 *     .rate(0.5f)      // Slow modulation
 *     .depth(5.0f)     // 5ms depth
 *     .voices(2)       // 2 voices
 *     .mix(0.5f);
 * @endcode
 */
class Chorus : public AudioEffect {
public:
    Chorus() = default;
    ~Chorus() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    Chorus& rate(float hz) {
        m_rateHz = std::max(0.1f, std::min(10.0f, hz));
        for (int i = 0; i < 4; ++i) {
            m_lfoL[i].setFrequency(m_rateHz);
            m_lfoR[i].setFrequency(m_rateHz);
        }
        return *this;
    }

    Chorus& depth(float ms) {
        m_depthMs = std::max(0.0f, std::min(20.0f, ms));
        return *this;
    }

    Chorus& voices(int n) {
        m_voices = std::max(1, std::min(4, n));
        return *this;
    }

    // Override base class methods to return Chorus&
    Chorus& input(const std::string& name) { AudioEffect::input(name); return *this; }
    Chorus& mix(float amount) { AudioEffect::mix(amount); return *this; }
    Chorus& bypass(bool b) { AudioEffect::bypass(b); return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name State Queries
    /// @{

    float getRate() const { return m_rateHz; }
    float getDepth() const { return m_depthMs; }
    int getVoices() const { return m_voices; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "Chorus"; }

    /// @}

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;
    void cleanupEffect() override;

private:
    // Parameters
    float m_rateHz = 0.5f;
    float m_depthMs = 5.0f;
    int m_voices = 2;

    // DSP
    dsp::DelayLine m_delayL;
    dsp::DelayLine m_delayR;
    dsp::LFO m_lfoL[4];
    dsp::LFO m_lfoR[4];
    uint32_t m_sampleRate = 48000;

    // Base delay for chorus effect (center point of modulation)
    static constexpr float BASE_DELAY_MS = 20.0f;
};

} // namespace vivid::audio
