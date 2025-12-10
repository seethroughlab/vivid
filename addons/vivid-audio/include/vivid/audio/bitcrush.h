#pragma once

/**
 * @file bitcrush.h
 * @brief Bitcrusher/sample rate reducer
 *
 * Creates lo-fi digital distortion by reducing
 * bit depth and/or sample rate.
 */

#include <vivid/audio/audio_effect.h>

namespace vivid::audio {

/**
 * @brief Bitcrusher/sample rate reducer
 *
 * Creates lo-fi, retro digital distortion by:
 * - Reducing bit depth (quantization noise)
 * - Reducing sample rate (aliasing)
 *
 * @par Parameters
 * - `bits(n)` - Bit depth (1-16, lower = more distortion)
 * - `sampleRate(hz)` - Target sample rate (100-48000)
 * - `mix(m)` - Dry/wet mix (0-1)
 *
 * @par Example
 * @code
 * chain.add<Bitcrush>("bitcrush")
 *     .input("audio")
 *     .bits(8)            // 8-bit audio
 *     .sampleRate(8000)   // 8kHz sample rate
 *     .mix(0.5f);
 * @endcode
 */
class Bitcrush : public AudioEffect {
public:
    Bitcrush() = default;
    ~Bitcrush() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    Bitcrush& bits(int n) {
        m_bits = std::max(1, std::min(16, n));
        m_quantLevels = static_cast<float>(1 << m_bits);
        return *this;
    }

    Bitcrush& sampleRate(float hz) {
        m_targetSampleRate = std::max(100.0f, std::min(48000.0f, hz));
        return *this;
    }

    // Override base class methods to return Bitcrush&
    Bitcrush& input(const std::string& name) { AudioEffect::input(name); return *this; }
    Bitcrush& mix(float amount) { AudioEffect::mix(amount); return *this; }
    Bitcrush& bypass(bool b) { AudioEffect::bypass(b); return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name State Queries
    /// @{

    int getBits() const { return m_bits; }
    float getSampleRate() const { return m_targetSampleRate; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "Bitcrush"; }

    /// @}

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;
    void cleanupEffect() override;

private:
    float quantize(float sample);

    // Parameters
    int m_bits = 8;
    float m_targetSampleRate = 8000.0f;
    float m_quantLevels = 256.0f;  // 2^8

    // State for sample rate reduction
    float m_holdL = 0.0f;
    float m_holdR = 0.0f;
    float m_sampleCounter = 0.0f;
    uint32_t m_sampleRate = 48000;
};

} // namespace vivid::audio
