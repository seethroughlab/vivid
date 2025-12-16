#pragma once

/**
 * @file gradient.h
 * @brief Gradient pattern generator
 *
 * Generates gradient patterns with configurable colors and shapes.
 */

#include <vivid/effects/simple_texture_effect.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Uniform buffer layout for Gradient effect
 */
struct GradientUniforms {
    int mode;
    float angle;
    float centerX;
    float centerY;
    float scale;
    float offset;
    float aspect;
    float _pad;
    float colorA[4];
    float colorB[4];
};

/**
 * @brief Gradient shape modes
 */
enum class GradientMode {
    Linear,    ///< Linear gradient (configurable angle)
    Radial,    ///< Circular gradient from center
    Angular,   ///< Conical sweep around center
    Diamond    ///< Diamond-shaped gradient
};

/**
 * @brief Gradient pattern generator
 *
 * Generates gradient patterns between two colors. Supports linear,
 * radial, angular, and diamond gradient shapes.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | angle | float | 0-2π | 0.0 | Gradient angle (linear mode) |
 * | scale | float | 0.1-10 | 1.0 | Gradient scale |
 * | offset | float | -1 to 1 | 0.0 | Gradient offset |
 * | center | vec2 | 0-1 | (0.5,0.5) | Center point for radial/angular |
 * | colorA | color | - | black | Start color |
 * | colorB | color | - | white | End color |
 *
 * @par Example
 * @code
 * chain.add<Gradient>("grad")
 *     .mode(GradientMode::Radial)
 *     .colorA(1.0f, 0.0f, 0.0f)   // Red
 *     .colorB(0.0f, 0.0f, 1.0f)   // Blue
 *     .scale(1.5f);
 * @endcode
 *
 * @par Inputs
 * None (generator)
 *
 * @par Output
 * Gradient texture
 */
class Gradient : public SimpleGeneratorEffect<Gradient, GradientUniforms> {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> angle{"angle", 0.0f, 0.0f, 6.283f};       ///< Gradient angle (linear mode)
    Param<float> scale{"scale", 1.0f, 0.1f, 10.0f};        ///< Gradient scale
    Param<float> offset{"offset", 0.0f, -1.0f, 1.0f};      ///< Gradient offset
    Vec2Param center{"center", 0.5f, 0.5f, 0.0f, 1.0f};    ///< Center point for radial/angular
    ColorParam colorA{"colorA", 0.0f, 0.0f, 0.0f, 1.0f};   ///< Start color
    ColorParam colorB{"colorB", 1.0f, 1.0f, 1.0f, 1.0f};   ///< End color

    /// @}
    // -------------------------------------------------------------------------

    Gradient() {
        registerParam(angle);
        registerParam(scale);
        registerParam(offset);
        registerParam(center);
        registerParam(colorA);
        registerParam(colorB);
    }

    /// @brief Set gradient mode (Linear, Radial, Angular, Diamond)
    void mode(GradientMode m) { if (m_mode != m) { m_mode = m; markDirty(); } }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "Gradient"; }

    /// @}

    // -------------------------------------------------------------------------
    /// @name CRTP Interface (required by SimpleGeneratorEffect)
    /// @{

    /**
     * @brief Returns uniform buffer values from current parameter state
     */
    GradientUniforms getUniforms() const {
        GradientUniforms uniforms = {};
        uniforms.mode = static_cast<int>(m_mode);
        uniforms.angle = angle;
        uniforms.centerX = center.x();
        uniforms.centerY = center.y();
        uniforms.scale = scale;
        uniforms.offset = offset;
        uniforms.aspect = static_cast<float>(m_width) / m_height;
        uniforms.colorA[0] = colorA.r();
        uniforms.colorA[1] = colorA.g();
        uniforms.colorA[2] = colorA.b();
        uniforms.colorA[3] = colorA.a();
        uniforms.colorB[0] = colorB.r();
        uniforms.colorB[1] = colorB.g();
        uniforms.colorB[2] = colorB.b();
        uniforms.colorB[3] = colorB.a();
        return uniforms;
    }

    /**
     * @brief Returns WGSL fragment shader source
     */
    const char* fragmentShader() const override;

    /// @}

private:
    GradientMode m_mode = GradientMode::Linear;
};

#ifdef _WIN32
extern template class SimpleGeneratorEffect<Gradient, GradientUniforms>;
#endif

} // namespace vivid::effects
