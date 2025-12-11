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
    Crackle() = default;
    ~Crackle() override = default;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set impulse density
     * @param d Density (0-1, probability per sample)
     */
    Crackle& density(float d) { m_density = d; return *this; }

    /**
     * @brief Set impulse volume
     * @param v Volume (0-1)
     */
    Crackle& volume(float v) { m_volume = v; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Crackle"; }

    std::vector<ParamDecl> params() override {
        return { m_density.decl(), m_volume.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "density") { out[0] = m_density; return true; }
        if (name == "volume") { out[0] = m_volume; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "density") { m_density = value[0]; return true; }
        if (name == "volume") { m_volume = value[0]; return true; }
        return false;
    }

    /// @}

private:
    float randomFloat();

    // Parameters
    Param<float> m_density{"density", 0.1f, 0.0f, 1.0f};
    Param<float> m_volume{"volume", 0.5f, 0.0f, 1.0f};

    // State
    uint32_t m_seed = 54321;

    bool m_initialized = false;
};

} // namespace vivid::audio
