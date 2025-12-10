#pragma once

/**
 * @file overdrive.h
 * @brief Overdrive/saturation effect
 *
 * Adds harmonic distortion using soft clipping
 * for a warm, tube-like character.
 */

#include <vivid/audio/audio_effect.h>
#include <vivid/audio/dsp/filters.h>

namespace vivid::audio {

/**
 * @brief Overdrive/saturation effect
 *
 * Adds harmonic distortion using soft clipping (tanh waveshaping)
 * for a warm, musical distortion character.
 *
 * @par Parameters
 * - `drive(d)` - Drive amount (1-10, more = more distortion)
 * - `tone(t)` - Tone control (0-1, 0=dark, 1=bright)
 * - `level(l)` - Output level (0-2)
 * - `mix(m)` - Dry/wet mix (0-1)
 *
 * @par Example
 * @code
 * chain.add<Overdrive>("overdrive")
 *     .input("audio")
 *     .drive(3.0f)      // Medium drive
 *     .tone(0.6f)       // Slightly bright
 *     .level(0.8f)      // Reduce output level
 *     .mix(1.0f);       // Fully wet
 * @endcode
 */
class Overdrive : public AudioEffect {
public:
    Overdrive() = default;
    ~Overdrive() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    Overdrive& drive(float d) {
        m_drive = std::max(1.0f, std::min(10.0f, d));
        return *this;
    }

    Overdrive& tone(float t) {
        m_tone = std::max(0.0f, std::min(1.0f, t));
        updateToneFilter();
        return *this;
    }

    Overdrive& level(float l) {
        m_level = std::max(0.0f, std::min(2.0f, l));
        return *this;
    }

    // Override base class methods to return Overdrive&
    Overdrive& input(const std::string& name) { AudioEffect::input(name); return *this; }
    Overdrive& mix(float amount) { AudioEffect::mix(amount); return *this; }
    Overdrive& bypass(bool b) { AudioEffect::bypass(b); return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name State Queries
    /// @{

    float getDrive() const { return m_drive; }
    float getTone() const { return m_tone; }
    float getLevel() const { return m_level; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "Overdrive"; }

    /// @}

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;
    void cleanupEffect() override;

private:
    void updateToneFilter();
    float saturate(float sample);

    // Parameters
    float m_drive = 3.0f;
    float m_tone = 0.5f;
    float m_level = 0.8f;

    // DSP
    dsp::OnePoleFilter m_toneFilterL;
    dsp::OnePoleFilter m_toneFilterR;
    uint32_t m_sampleRate = 48000;
};

} // namespace vivid::audio
