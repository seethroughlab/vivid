#pragma once

/**
 * @file barrel_distortion.h
 * @brief CRT-style barrel distortion effect
 *
 * Warps the image to simulate the curved glass of a CRT monitor.
 */

#include <vivid/effects/simple_texture_effect.h>
#include <vivid/param.h>

namespace vivid::effects {

/// @brief Uniform buffer for BarrelDistortion effect
struct BarrelDistortionUniforms {
    float curvature;
    float _pad[3];
};

/**
 * @brief Barrel distortion for CRT curvature simulation
 *
 * Applies a barrel distortion that curves the image edges inward,
 * simulating how CRT monitors had curved glass screens.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | curvature | float | 0-1 | 0.1 | Distortion amount (0 = none, 1 = extreme) |
 *
 * @par Example
 * @code
 * chain.add<BarrelDistortion>("barrel")
 *     .input("source")
 *     .curvature(0.08f);
 * @endcode
 */
class BarrelDistortion : public SimpleTextureEffect<BarrelDistortion, BarrelDistortionUniforms> {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> curvature{"curvature", 0.1f, 0.0f, 1.0f}; ///< Distortion amount

    /// @}
    // -------------------------------------------------------------------------

    BarrelDistortion() {
        registerParam(curvature);
    }

    /// @brief Set input texture
    void input(TextureOperator* op) { setInput(0, op); }

    /// @brief Get uniform values for GPU
    BarrelDistortionUniforms getUniforms() const {
        return {curvature, {0, 0, 0}};
    }

    std::string name() const override { return "BarrelDistortion"; }

    /// @brief Fragment shader source (used by CRTP base)
    const char* fragmentShader() const override;
};

#ifdef _WIN32
extern template class SimpleTextureEffect<BarrelDistortion, BarrelDistortionUniforms>;
#endif

} // namespace vivid::effects
