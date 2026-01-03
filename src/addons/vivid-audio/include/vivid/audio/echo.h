#pragma once

/**
 * @file echo.h
 * @brief Multi-tap echo effect
 *
 * Echo creates multiple delayed copies with exponential decay,
 * simulating a natural echo in a large space.
 */

#include <vivid/audio/audio_effect.h>
#include <vivid/audio/dsp/delay_line.h>
#include <vivid/param.h>

namespace vivid::audio {

/**
 * @brief Multi-tap echo effect
 *
 * Creates multiple delayed copies of the signal with
 * exponentially decaying levels.
 *
 * @par Parameters
 * - `delayTime(ms)` - Base delay time (0-2000ms)
 * - `decay(d)` - Decay per tap (0-1, higher = longer tail)
 * - `taps(n)` - Number of echo taps (1-8)
 * - `mix(m)` - Dry/wet mix (0-1)
 *
 * @par Example
 * @code
 * chain.add<Echo>("echo")
 *     .input("audio")
 *     .delayTime(300)   // 300ms between echoes
 *     .decay(0.6f)      // Each echo is 60% of previous
 *     .taps(4)          // 4 echo repeats
 *     .mix(0.5f);
 * @endcode
 */
class Echo : public AudioEffect {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> delayTime{"delayTime", 300.0f, 0.0f, 2000.0f};  ///< Delay time in ms
    Param<float> decay{"decay", 0.5f, 0.0f, 0.95f};              ///< Decay per tap
    Param<int> taps{"taps", 4, 1, 8};                            ///< Number of echo taps
    Param<float> mix{"mix", 0.5f, 0.0f, 1.0f};                   ///< Dry/wet mix

    /// @}
    // -------------------------------------------------------------------------

    Echo() {
        registerParam(delayTime);
        registerParam(decay);
        registerParam(taps);
        registerParam(mix);
    }
    ~Echo() override = default;

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "Echo"; }

    /// @}

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;
    void cleanupEffect() override;

    // DSP
    dsp::StereoDelayLine m_delayLine;
    uint32_t m_sampleRate = 48000;
};

} // namespace vivid::audio
