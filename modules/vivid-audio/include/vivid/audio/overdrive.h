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
#include <vivid/param.h>

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
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> drive{"drive", 3.0f, 1.0f, 10.0f};     ///< Drive amount
    Param<float> tone{"tone", 0.5f, 0.0f, 1.0f};        ///< Tone (0=dark, 1=bright)
    Param<float> level{"level", 0.8f, 0.0f, 2.0f};      ///< Output level
    Param<float> mix{"mix", 1.0f, 0.0f, 1.0f};          ///< Dry/wet mix

    /// @}
    // -------------------------------------------------------------------------

    Overdrive() {
        registerParam(drive);
        registerParam(tone);
        registerParam(level);
        registerParam(mix);
    }
    ~Overdrive() override = default;

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

    // DSP
    dsp::OnePoleFilter m_toneFilterL;
    dsp::OnePoleFilter m_toneFilterR;
    uint32_t m_sampleRate = 48000;
    float m_cachedTone = 0.5f;  // For detecting tone changes
};

} // namespace vivid::audio
