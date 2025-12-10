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
    Echo() = default;
    ~Echo() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    Echo& delayTime(float ms) {
        m_delayTimeMs = std::max(0.0f, std::min(2000.0f, ms));
        return *this;
    }

    Echo& decay(float d) {
        m_decay = std::max(0.0f, std::min(0.95f, d));
        return *this;
    }

    Echo& taps(int n) {
        m_taps = std::max(1, std::min(8, n));
        return *this;
    }

    // Override base class methods to return Echo&
    Echo& input(const std::string& name) { AudioEffect::input(name); return *this; }
    Echo& mix(float amount) { AudioEffect::mix(amount); return *this; }
    Echo& bypass(bool b) { AudioEffect::bypass(b); return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name State Queries
    /// @{

    float getDelayTime() const { return m_delayTimeMs; }
    float getDecay() const { return m_decay; }
    int getTaps() const { return m_taps; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "Echo"; }

    /// @}

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;
    void cleanupEffect() override;

private:
    // Parameters
    float m_delayTimeMs = 300.0f;
    float m_decay = 0.5f;
    int m_taps = 4;

    // DSP
    dsp::StereoDelayLine m_delayLine;
    uint32_t m_sampleRate = 48000;
};

} // namespace vivid::audio
