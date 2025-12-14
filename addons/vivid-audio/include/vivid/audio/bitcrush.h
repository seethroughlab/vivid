#pragma once

/**
 * @file bitcrush.h
 * @brief Bitcrusher/sample rate reducer
 *
 * Creates lo-fi digital distortion by reducing
 * bit depth and/or sample rate.
 */

#include <vivid/audio/audio_effect.h>
#include <vivid/param.h>

namespace vivid::audio {

/**
 * @brief Bitcrusher/sample rate reducer
 *
 * Creates lo-fi, retro digital distortion by:
 * - Reducing bit depth (quantization noise)
 * - Reducing sample rate (aliasing)
 *
 * @par Parameters
 * - `bits` - Bit depth (1-16, lower = more distortion)
 * - `targetSampleRate` - Target sample rate (100-48000)
 * - `mix` - Dry/wet mix (0-1)
 *
 * @par Example
 * @code
 * chain.add<Bitcrush>("bitcrush").input("audio");
 * auto* bc = chain.get<Bitcrush>("bitcrush");
 * bc->bits = 8;               // 8-bit audio
 * bc->targetSampleRate = 8000.0f;  // 8kHz sample rate
 * bc->mix = 0.5f;
 * @endcode
 */
class Bitcrush : public AudioEffect {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<int> bits{"bits", 8, 1, 16};                           ///< Bit depth (1-16)
    Param<float> targetSampleRate{"targetSampleRate", 8000.0f, 100.0f, 48000.0f}; ///< Target sample rate
    Param<float> mix{"mix", 1.0f, 0.0f, 1.0f};                   ///< Dry/wet mix

    /// @}
    // -------------------------------------------------------------------------

    Bitcrush() {
        registerParam(bits);
        registerParam(targetSampleRate);
        registerParam(mix);
    }
    ~Bitcrush() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    // Override base class methods to return Bitcrush&
    Bitcrush& input(const std::string& name) { AudioEffect::input(name); return *this; }
    Bitcrush& bypass(bool b) { AudioEffect::bypass(b); return *this; }

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

    // State for sample rate reduction
    float m_holdL = 0.0f;
    float m_holdR = 0.0f;
    float m_sampleCounter = 0.0f;
    uint32_t m_sampleRate = 48000;
};

} // namespace vivid::audio
