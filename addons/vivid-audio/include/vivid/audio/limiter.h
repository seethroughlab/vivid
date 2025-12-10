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
    Limiter() = default;
    ~Limiter() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    Limiter& ceiling(float dB) {
        m_ceilingDb = std::max(-20.0f, std::min(0.0f, dB));
        m_ceilingLinear = dsp::EnvelopeFollower::dbToLinear(m_ceilingDb);
        return *this;
    }

    Limiter& release(float ms) {
        m_releaseMs = std::max(10.0f, std::min(1000.0f, ms));
        if (m_initialized) {
            m_envelope.setRelease(m_releaseMs);
        }
        return *this;
    }

    // Override base class methods to return Limiter&
    Limiter& input(const std::string& name) { AudioEffect::input(name); return *this; }
    Limiter& mix(float amount) { AudioEffect::mix(amount); return *this; }
    Limiter& bypass(bool b) { AudioEffect::bypass(b); return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name State Queries
    /// @{

    float getCeiling() const { return m_ceilingDb; }
    float getRelease() const { return m_releaseMs; }
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
    // Parameters
    float m_ceilingDb = -0.3f;
    float m_ceilingLinear = 0.966f;
    float m_releaseMs = 100.0f;

    // State
    dsp::EnvelopeFollower m_envelope;
    float m_currentGainReductionDb = 0.0f;
    bool m_initialized = false;
};

} // namespace vivid::audio
