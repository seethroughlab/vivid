#pragma once

/**
 * @file compressor.h
 * @brief Dynamic range compressor
 *
 * Reduces the dynamic range of audio by attenuating
 * loud signals that exceed a threshold.
 */

#include <vivid/audio/audio_effect.h>
#include <vivid/audio/dsp/envelope.h>
#include <vivid/param.h>

namespace vivid::audio {

/**
 * @brief Dynamic range compressor
 *
 * Reduces the dynamic range by reducing the gain of
 * signals that exceed a threshold.
 *
 * @par Parameters
 * - `threshold(dB)` - Level above which compression starts (-60 to 0)
 * - `ratio(r)` - Compression ratio (1=no compression, 20=hard limiting)
 * - `attack(ms)` - Attack time (0.1-100ms)
 * - `release(ms)` - Release time (10-1000ms)
 * - `makeupGain(dB)` - Output gain boost to compensate for compression
 * - `mix(m)` - Dry/wet mix (0-1)
 *
 * @par Example
 * @code
 * chain.add<Compressor>("comp")
 *     .input("audio")
 *     .threshold(-12)     // Compress above -12 dB
 *     .ratio(4)           // 4:1 compression
 *     .attack(10)         // 10ms attack
 *     .release(100)       // 100ms release
 *     .makeupGain(6);     // +6dB makeup gain
 * @endcode
 */
class Compressor : public AudioEffect {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> threshold{"threshold", -12.0f, -60.0f, 0.0f};    ///< Threshold in dB
    Param<float> ratio{"ratio", 4.0f, 1.0f, 20.0f};               ///< Compression ratio
    Param<float> attack{"attack", 10.0f, 0.1f, 100.0f};           ///< Attack time in ms
    Param<float> release{"release", 100.0f, 10.0f, 1000.0f};      ///< Release time in ms
    Param<float> makeupGain{"makeupGain", 0.0f, -20.0f, 40.0f};   ///< Makeup gain in dB
    Param<float> knee{"knee", 0.0f, 0.0f, 12.0f};                 ///< Knee width in dB
    Param<float> mix{"mix", 1.0f, 0.0f, 1.0f};                    ///< Dry/wet mix

    /// @}
    // -------------------------------------------------------------------------

    Compressor() {
        registerParam(threshold);
        registerParam(ratio);
        registerParam(attack);
        registerParam(release);
        registerParam(makeupGain);
        registerParam(knee);
        registerParam(mix);
    }
    ~Compressor() override = default;

    // -------------------------------------------------------------------------
    /// @name State Queries
    /// @{

    float getGainReduction() const { return m_currentGainReductionDb; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "Compressor"; }

    // Custom visualization
    bool drawVisualization(VizDrawList* drawList, float minX, float minY,
                           float maxX, float maxY) override;

    /// @}

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;
    void cleanupEffect() override;

private:
    float computeGain(float inputDb);

    // State
    dsp::EnvelopeFollower m_envelope;
    float m_currentGainReductionDb = 0.0f;
    float m_cachedAttack = 10.0f;
    float m_cachedRelease = 100.0f;
};

} // namespace vivid::audio
