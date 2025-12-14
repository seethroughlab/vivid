#pragma once

/**
 * @file shape.h
 * @brief SDF-based shape generator
 *
 * Generates geometric shapes using signed distance fields.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Shape types
 */
enum class ShapeType {
    Circle,       ///< Circular shape
    Rectangle,    ///< Sharp-cornered rectangle
    RoundedRect,  ///< Rectangle with rounded corners
    Triangle,     ///< Equilateral triangle
    Star,         ///< Multi-pointed star
    Ring,         ///< Hollow circle (donut)
    Polygon       ///< Regular polygon with N sides
};

/**
 * @brief SDF-based shape generator
 *
 * Generates geometric shapes using signed distance fields (SDFs).
 * Produces anti-aliased shapes with configurable softness and color.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | size | vec2 | 0-2 | (0.5,0.5) | Shape size |
 * | position | vec2 | 0-1 | (0.5,0.5) | Center position |
 * | rotation | float | -2π to 2π | 0.0 | Rotation angle |
 * | sides | int | 3-32 | 5 | Polygon/star point count |
 * | cornerRadius | float | 0-0.5 | 0.0 | Corner rounding |
 * | thickness | float | 0-0.5 | 0.1 | Ring/outline thickness |
 * | softness | float | 0-0.2 | 0.01 | Edge softness |
 * | color | color | - | white | Shape color |
 *
 * @par Example
 * @code
 * chain.add<Shape>("circle")
 *     .type(ShapeType::Circle)
 *     .size(0.3f)
 *     .position(0.5f, 0.5f)
 *     .color(1.0f, 0.5f, 0.0f);  // Orange
 * @endcode
 *
 * @par Inputs
 * None (generator)
 *
 * @par Output
 * Shape texture with alpha
 */
class Shape : public TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Vec2Param size{"size", 0.5f, 0.5f, 0.0f, 2.0f};              ///< Shape size
    Vec2Param position{"position", 0.5f, 0.5f, 0.0f, 1.0f};      ///< Center position
    Param<float> rotation{"rotation", 0.0f, -6.28f, 6.28f};      ///< Rotation angle
    Param<int> sides{"sides", 5, 3, 32};                          ///< Polygon/star point count
    Param<float> cornerRadius{"cornerRadius", 0.0f, 0.0f, 0.5f}; ///< Corner rounding
    Param<float> thickness{"thickness", 0.1f, 0.0f, 0.5f};       ///< Ring/outline thickness
    Param<float> softness{"softness", 0.01f, 0.0f, 0.2f};        ///< Edge softness
    ColorParam color{"color", 1.0f, 1.0f, 1.0f, 1.0f};           ///< Shape color

    /// @}
    // -------------------------------------------------------------------------

    Shape() {
        registerParam(size);
        registerParam(position);
        registerParam(rotation);
        registerParam(sides);
        registerParam(cornerRadius);
        registerParam(thickness);
        registerParam(softness);
        registerParam(color);
    }
    ~Shape() override;

    /// @brief Set shape type (Circle, Rectangle, RoundedRect, etc.)
    void type(ShapeType t) { if (m_type != t) { m_type = t; markDirty(); } }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Shape"; }

    /// @}

private:
    void createPipeline(Context& ctx);

    ShapeType m_type = ShapeType::Circle;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
