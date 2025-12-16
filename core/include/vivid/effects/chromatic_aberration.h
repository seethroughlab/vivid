#pragma once

/**
 * @file chromatic_aberration.h
 * @brief RGB channel separation effect
 *
 * Simulates lens chromatic aberration by offsetting color channels.
 */

#include <vivid/effects/simple_texture_effect.h>
#include <vivid/param.h>

namespace vivid::effects {

/// @brief Uniform buffer for ChromaticAberration effect
struct ChromaticAberrationUniforms {
    float amount;
    float angle;
    int radial;
    float _pad;
};

/**
 * @brief RGB channel separation effect
 *
 * Offsets the red, green, and blue color channels to create a
 * chromatic aberration effect. Supports both linear and radial modes.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | amount | float | 0-0.1 | 0.01 | Separation distance |
 * | angle | float | -2π to 2π | 0.0 | Direction angle (linear mode) |
 * | radial | bool | - | true | Use radial vs linear separation |
 *
 * @par Example
 * @code
 * chain.add<ChromaticAberration>("aberration")
 *     .input("source")
 *     .amount(0.02f)
 *     .radial(true);
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Texture with RGB channel separation
 */
class ChromaticAberration : public SimpleTextureEffect<ChromaticAberration, ChromaticAberrationUniforms> {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> amount{"amount", 0.01f, 0.0f, 0.1f};  ///< Separation amount
    Param<float> angle{"angle", 0.0f, -6.28f, 6.28f};  ///< Direction angle (linear mode)
    Param<bool> radial{"radial", true};                 ///< Radial vs linear mode

    /// @}
    // -------------------------------------------------------------------------

    ChromaticAberration() {
        registerParam(amount);
        registerParam(angle);
        registerParam(radial);
    }

    /// @brief Set input texture
    void input(TextureOperator* op) { setInput(0, op); }

    /// @brief Get uniform values for GPU
    ChromaticAberrationUniforms getUniforms() const {
        return {amount, angle, radial ? 1 : 0, 0.0f};
    }

    std::string name() const override { return "ChromaticAberration"; }

    /// @brief Fragment shader source (used by CRTP base)
    const char* fragmentShader() const override;
};

#ifdef _WIN32
extern template class SimpleTextureEffect<ChromaticAberration, ChromaticAberrationUniforms>;
#endif

} // namespace vivid::effects
