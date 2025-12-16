#pragma once

/**
 * @file vignette.h
 * @brief Edge darkening vignette effect
 *
 * Darkens the edges and corners of the image, simulating the
 * natural light falloff of camera lenses.
 */

#include <vivid/effects/simple_texture_effect.h>
#include <vivid/param.h>

namespace vivid::effects {

/// @brief Uniform buffer for Vignette effect
struct VignetteUniforms {
    float intensity;
    float softness;
    float roundness;
    float aspect;
};

/**
 * @brief Edge darkening vignette effect
 *
 * Creates a gradual darkening from the center to the edges of the image,
 * simulating the light falloff seen in camera lenses and CRT monitors.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | intensity | float | 0-2 | 0.5 | Darkening strength |
 * | softness | float | 0-2 | 0.5 | Edge softness/gradient width |
 * | roundness | float | 0-1 | 1.0 | Shape: 0 = rectangular, 1 = circular |
 *
 * @par Example
 * @code
 * chain.add<Vignette>("vignette")
 *     .input("source")
 *     .intensity(0.4f)
 *     .softness(0.6f);
 * @endcode
 */
class Vignette : public SimpleTextureEffect<Vignette, VignetteUniforms> {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> intensity{"intensity", 0.5f, 0.0f, 2.0f};  ///< Darkening strength
    Param<float> softness{"softness", 0.5f, 0.0f, 2.0f};    ///< Edge gradient width
    Param<float> roundness{"roundness", 1.0f, 0.0f, 1.0f};  ///< 0=rectangular, 1=circular

    /// @}
    // -------------------------------------------------------------------------

    Vignette() {
        registerParam(intensity);
        registerParam(softness);
        registerParam(roundness);
    }

    /// @brief Set input texture
    void input(TextureOperator* op) { setInput(0, op); }

    /// @brief Get uniform values for GPU
    VignetteUniforms getUniforms() const {
        return {
            intensity,
            softness,
            roundness,
            static_cast<float>(m_width) / static_cast<float>(m_height)
        };
    }

    std::string name() const override { return "Vignette"; }

    /// @brief Fragment shader source (used by CRTP base)
    const char* fragmentShader() const override;
};

#ifdef _WIN32
extern template class SimpleTextureEffect<Vignette, VignetteUniforms>;
#endif

} // namespace vivid::effects
