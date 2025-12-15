#pragma once

/**
 * @file solid_color.h
 * @brief Solid color generator
 *
 * Generates a uniform solid color texture.
 */

#include <vivid/effects/simple_texture_effect.h>
#include <vivid/param.h>

namespace vivid::effects {

struct SolidColorUniforms {
    float r, g, b, a;
};

/**
 * @brief Solid color generator
 *
 * Generates a texture filled with a single uniform color.
 * Useful as a background or for masking operations.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | color | color | - | black | Fill color (RGBA) |
 *
 * @par Example
 * @code
 * chain.add<SolidColor>("bg")
 *     .color(0.1f, 0.1f, 0.2f);  // Dark blue background
 * @endcode
 *
 * @par Inputs
 * None (generator)
 *
 * @par Output
 * Solid color texture
 */
class SolidColor : public SimpleGeneratorEffect<SolidColor, SolidColorUniforms> {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    ColorParam color{"color", 0.0f, 0.0f, 0.0f, 1.0f}; ///< Fill color (RGBA)

    /// @}
    // -------------------------------------------------------------------------

    SolidColor() {
        registerParam(color);
    }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "SolidColor"; }

    SolidColorUniforms getUniforms() const {
        return {color.r(), color.g(), color.b(), color.a()};
    }

    const char* fragmentShader() const override;

    /// @}
};

} // namespace vivid::effects
