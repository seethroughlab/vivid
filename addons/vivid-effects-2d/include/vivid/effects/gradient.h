#pragma once

/**
 * @file gradient.h
 * @brief Gradient pattern generator
 *
 * Generates gradient patterns with configurable colors and shapes.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

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
class Gradient : public TextureOperator {
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
    ~Gradient() override;

    /// @brief Set gradient mode (Linear, Radial, Angular, Diamond)
    Gradient& mode(GradientMode m) { if (m_mode != m) { m_mode = m; markDirty(); } return *this; }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Gradient"; }

    /// @}

private:
    void createPipeline(Context& ctx);

    GradientMode m_mode = GradientMode::Linear;

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
