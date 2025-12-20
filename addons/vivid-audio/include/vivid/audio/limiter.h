#pragma once

/**
 * @file limiter.h
 * @brief Brick-wall limiter
 *
 * Prevents audio from exceeding a ceiling level.
 * Similar to an infinite-ratio compressor with fast attack.
 */

#include <vivid/audio/audio_effect.h>
#include <vivid/audio/dsp/envelope.h>
#include <vivid/param.h>

namespace vivid::audio {

/**
 * @brief Brick-wall limiter
 *
 * Prevents audio from exceeding a specified ceiling level.
 * Uses fast attack and adjustable release.
 *
 * @par Parameters
 * - `ceiling(dB)` - Maximum output level (-20 to 0 dB)
 * - `release(ms)` - Release time (10-1000ms)
 * - `mix(m)` - Dry/wet mix (0-1)
 *
 * @par Example
 * @code
 * chain.add<Limiter>("limiter")
 *     .input("audio")
 *     .ceiling(-0.3f)    // Limit to -0.3 dB (prevent clipping)
 *     .release(100);     // 100ms release
 * @endcode
 */
class Limiter : public AudioEffect {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> ceiling{"ceiling", -0.3f, -20.0f, 0.0f};       ///< Ceiling level in dB
    Param<float> release{"release", 100.0f, 10.0f, 1000.0f};    ///< Release time in ms
    Param<float> mix{"mix", 1.0f, 0.0f, 1.0f};                  ///< Dry/wet mix

    /// @}
    // -------------------------------------------------------------------------

    Limiter() {
        registerParam(ceiling);
        registerParam(release);
        registerParam(mix);
    }
    ~Limiter() override = default;

    // -------------------------------------------------------------------------
    /// @name State Queries
    /// @{

    float getGainReduction() const { return m_currentGainReductionDb; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "Limiter"; }

    /// @}

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;
    void cleanupEffect() override;

private:
    // State
    dsp::EnvelopeFollower m_envelope;
    float m_currentGainReductionDb = 0.0f;
    float m_cachedRelease = 100.0f;
};

} // namespace vivid::audio
