#pragma once

/**
 * @file crackle.h
 * @brief Random impulse/crackle generator
 *
 * Generates random impulses for vinyl crackle, glitch effects, and texture.
 */

#include <vivid/audio_operator.h>
#include <vivid/param.h>
#include <string>
#include <vector>

namespace vivid::audio {

/**
 * @brief Random impulse/crackle generator
 *
 * Generates random clicks and pops at controllable density. Useful for
 * vinyl texture, glitch effects, or as triggers for other synthesis.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | density | float | 0-1 | 0.1 | Probability of impulse per sample |
 * | volume | float | 0-1 | 0.5 | Impulse amplitude |
 *
 * @par Example
 * @code
 * // Vinyl crackle texture
 * chain.add<Crackle>("crackle").density(0.001f).volume(0.1f);
 * @endcode
 */
class Crackle : public AudioOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> density{"density", 0.1f, 0.0f, 1.0f};  ///< Impulse density (probability per sample)
    Param<float> volume{"volume", 0.5f, 0.0f, 1.0f};    ///< Impulse amplitude

    /// @}
    // -------------------------------------------------------------------------

    Crackle() {
        registerParam(density);
        registerParam(volume);
    }
    ~Crackle() override = default;

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Crackle"; }

    /// @}

private:
    float randomFloat();

    // State
    uint32_t m_seed = 54321;
};

} // namespace vivid::audio
