#pragma once

/**
 * @file gate.h
 * @brief Noise gate
 *
 * Attenuates audio below a threshold level,
 * useful for removing background noise.
 */

#include <vivid/audio/audio_effect.h>
#include <vivid/audio/dsp/envelope.h>
#include <vivid/param.h>

namespace vivid::audio {

/**
 * @brief Noise gate
 *
 * Silences audio that falls below a threshold level.
 * Useful for removing background noise between phrases.
 *
 * @par Parameters
 * - `threshold(dB)` - Level below which audio is gated (-60 to 0)
 * - `attack(ms)` - Attack time (0.1-100ms)
 * - `hold(ms)` - Hold time before release (0-500ms)
 * - `release(ms)` - Release time (10-1000ms)
 * - `range(dB)` - Amount of attenuation when gated (0 to -inf)
 * - `mix(m)` - Dry/wet mix (0-1)
 *
 * @par Example
 * @code
 * chain.add<Gate>("gate")
 *     .input("audio")
 *     .threshold(-40)    // Gate below -40 dB
 *     .attack(1)         // Fast attack
 *     .hold(50)          // 50ms hold
 *     .release(100)      // 100ms release
 *     .range(-80);       // Reduce to -80 dB when gated
 * @endcode
 */
class Gate : public AudioEffect {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> threshold{"threshold", -40.0f, -80.0f, 0.0f};    ///< Threshold in dB
    Param<float> attack{"attack", 1.0f, 0.1f, 100.0f};            ///< Attack time in ms
    Param<float> hold{"hold", 50.0f, 0.0f, 500.0f};               ///< Hold time in ms
    Param<float> release{"release", 100.0f, 10.0f, 1000.0f};      ///< Release time in ms
    Param<float> range{"range", -80.0f, -80.0f, 0.0f};            ///< Attenuation range in dB
    Param<float> mix{"mix", 1.0f, 0.0f, 1.0f};                    ///< Dry/wet mix

    /// @}
    // -------------------------------------------------------------------------

    Gate() {
        registerParam(threshold);
        registerParam(attack);
        registerParam(hold);
        registerParam(release);
        registerParam(range);
        registerParam(mix);
    }
    ~Gate() override = default;

    // -------------------------------------------------------------------------
    /// @name State Queries
    /// @{

    bool isOpen() const { return m_gateOpen; }
    float gateGain() const { return m_gateGain; }  ///< Current gate gain (0-1)

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "Gate"; }

    // Custom visualization
    bool drawVisualization(ImDrawList* drawList, float minX, float minY,
                           float maxX, float maxY) override;

    /// @}

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;
    void cleanupEffect() override;

private:
    // State
    dsp::EnvelopeFollower m_envelope;
    float m_gateGain = 0.0f;  // Current gate gain (0 to 1)
    float m_holdCounter = 0.0f;
    bool m_gateOpen = false;
    uint32_t m_sampleRate = 48000;
};

} // namespace vivid::audio
