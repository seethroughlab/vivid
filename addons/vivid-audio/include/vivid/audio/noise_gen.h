#pragma once

/**
 * @file noise_gen.h
 * @brief Noise generator for synthesis and textures
 *
 * Generates various types of noise useful for percussion, textures, and modulation.
 */

#include <vivid/audio_operator.h>
#include <vivid/param.h>
#include <string>
#include <vector>
#include <cstdint>

namespace vivid::audio {

/**
 * @brief Noise color types
 */
enum class NoiseColor {
    White,      ///< Equal energy per frequency (bright, harsh)
    Pink,       ///< Equal energy per octave (natural, balanced)
    Brown       ///< -6dB/octave rolloff (deep, rumbling)
};

/**
 * @brief Noise generator for synthesis
 *
 * Generates colored noise useful for hi-hats, snares, wind, and textures.
 * White noise has equal energy at all frequencies, pink noise has equal
 * energy per octave (more natural), and brown noise emphasizes low frequencies.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | volume | float | 0-1 | 0.5 | Output amplitude |
 *
 * @par Example
 * @code
 * // White noise for hi-hat
 * chain.add<NoiseGen>("noise").color(NoiseColor::White).volume(0.3f);
 * chain.add<Decay>("env").input("noise").time(0.05f);
 * @endcode
 */
class NoiseGen : public AudioOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> volume{"volume", 0.5f, 0.0f, 1.0f};  ///< Output amplitude

    /// @}
    // -------------------------------------------------------------------------

    NoiseGen() {
        registerParam(volume);
    }
    ~NoiseGen() override = default;

    /// @brief Set noise color
    void setColor(NoiseColor c) { m_color = c; }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "NoiseGen"; }

    /// @}

private:
    float generateWhite();
    float generatePink();
    float generateBrown();

    // Noise color (enum, not a Param)
    NoiseColor m_color = NoiseColor::White;

    // State
    uint32_t m_seed = 12345;  // PRNG state

    // Pink noise filter state (Paul Kellet's algorithm)
    float m_b0 = 0, m_b1 = 0, m_b2 = 0, m_b3 = 0, m_b4 = 0, m_b5 = 0, m_b6 = 0;

    // Brown noise state
    float m_lastBrown = 0.0f;

    bool m_initialized = false;
};

} // namespace vivid::audio
