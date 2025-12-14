#pragma once

/**
 * @file reverb.h
 * @brief Freeverb-style algorithmic reverb
 *
 * Simulates the acoustic properties of a room using
 * parallel comb filters and series all-pass filters.
 */

#include <vivid/audio/audio_effect.h>
#include <vivid/audio/dsp/filters.h>
#include <vivid/param.h>
#include <array>

namespace vivid::audio {

/**
 * @brief Freeverb-style algorithmic reverb
 *
 * Creates a realistic room simulation using:
 * - 8 parallel comb filters (for density)
 * - 4 series all-pass filters (for diffusion)
 *
 * @par Parameters
 * - `roomSize` - Room size (0-1, larger = longer tail)
 * - `damping` - High frequency damping (0-1)
 * - `width` - Stereo width (0-1)
 * - `mix` - Dry/wet mix (0-1)
 *
 * @par Example
 * @code
 * chain.add<Reverb>("reverb").input("audio");
 * auto* reverb = chain.get<Reverb>("reverb");
 * reverb->roomSize = 0.7f;    // Large room
 * reverb->damping = 0.5f;     // Moderate damping
 * reverb->width = 1.0f;       // Full stereo
 * reverb->mix = 0.3f;         // 30% wet
 * @endcode
 */
class Reverb : public AudioEffect {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> roomSize{"roomSize", 0.5f, 0.0f, 1.0f};  ///< Room size (larger = longer tail)
    Param<float> damping{"damping", 0.5f, 0.0f, 1.0f};    ///< High frequency damping
    Param<float> width{"width", 1.0f, 0.0f, 1.0f};        ///< Stereo width
    Param<float> mix{"mix", 0.3f, 0.0f, 1.0f};            ///< Dry/wet mix

    /// @}
    // -------------------------------------------------------------------------

    Reverb() {
        registerParam(roomSize);
        registerParam(damping);
        registerParam(width);
        registerParam(mix);
    }
    ~Reverb() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    // Override base class methods to return Reverb&
    Reverb& input(const std::string& name) { AudioEffect::input(name); return *this; }
    Reverb& bypass(bool b) { AudioEffect::bypass(b); return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "Reverb"; }

    /// @}

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;
    void cleanupEffect() override;

private:
    void updateParameters();

    // Freeverb constants (delay lengths in samples at 44.1kHz, scaled for 48kHz)
    static constexpr int NUM_COMBS = 8;
    static constexpr int NUM_ALLPASS = 4;

    // Comb filter delay times (scaled from 44.1kHz to 48kHz)
    static constexpr int COMB_DELAYS_L[NUM_COMBS] = {
        1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617
    };
    static constexpr int COMB_DELAYS_R[NUM_COMBS] = {
        1116 + 23, 1188 + 23, 1277 + 23, 1356 + 23,
        1422 + 23, 1491 + 23, 1557 + 23, 1617 + 23
    };

    // All-pass delay times
    static constexpr int ALLPASS_DELAYS[NUM_ALLPASS] = {
        556, 441, 341, 225
    };

    // DSP
    std::array<dsp::CombFilter, NUM_COMBS> m_combsL;
    std::array<dsp::CombFilter, NUM_COMBS> m_combsR;
    std::array<dsp::AllPassDelay, NUM_ALLPASS> m_allpassL;
    std::array<dsp::AllPassDelay, NUM_ALLPASS> m_allpassR;

    uint32_t m_sampleRate = 48000;
};

} // namespace vivid::audio
