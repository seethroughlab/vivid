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
#include <vivid/param.h>

namespace vivid::audio {

/**
 * @brief Chorus effect
 *
 * Creates a fuller, wider sound by mixing the original
 * with delayed, pitch-modulated copies.
 *
 * @par Parameters
 * - `rate` - LFO rate (0.1-10 Hz)
 * - `depth` - Modulation depth (0-20ms)
 * - `voices` - Number of chorus voices (1-4)
 * - `mix` - Dry/wet mix (0-1)
 *
 * @par Example
 * @code
 * chain.add<Chorus>("chorus").input("audio");
 * auto* chorus = chain.get<Chorus>("chorus");
 * chorus->rate = 0.5f;      // Slow modulation
 * chorus->depth = 5.0f;     // 5ms depth
 * chorus->voices = 2;       // 2 voices
 * chorus->mix = 0.5f;
 * @endcode
 */
class Chorus : public AudioEffect {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> rate{"rate", 0.5f, 0.1f, 10.0f};      ///< LFO rate in Hz
    Param<float> depth{"depth", 5.0f, 0.0f, 20.0f};    ///< Modulation depth in ms
    Param<int> voices{"voices", 2, 1, 4};              ///< Number of chorus voices
    Param<float> mix{"mix", 0.5f, 0.0f, 1.0f};         ///< Dry/wet mix

    /// @}
    // -------------------------------------------------------------------------

    Chorus() {
        registerParam(rate);
        registerParam(depth);
        registerParam(voices);
        registerParam(mix);
    }
    ~Chorus() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    // Override base class methods to return Chorus&
    Chorus& input(const std::string& name) { AudioEffect::input(name); return *this; }
    Chorus& bypass(bool b) { AudioEffect::bypass(b); return *this; }

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
