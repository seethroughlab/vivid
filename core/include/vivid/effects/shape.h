#pragma once

/**
 * @file shape.h
 * @brief SDF-based shape generator
 *
 * Generates geometric shapes using signed distance fields.
 */

#include <vivid/effects/simple_texture_effect.h>
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
 * @brief Uniform buffer for Shape effect
 */
struct ShapeUniforms {
    int shapeType;
    float sizeX;
    float sizeY;
    float posX;
    float posY;
    float rotation;
    int sides;
    float cornerRadius;
    float thickness;
    float softness;
    float colorR;
    float colorG;
    float colorB;
    float colorA;
    float aspect;
    float _pad;
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
class Shape : public SimpleGeneratorEffect<Shape, ShapeUniforms> {
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

    /// @brief Set shape type (Circle, Rectangle, RoundedRect, etc.)
    void type(ShapeType t) { if (m_type != t) { m_type = t; markDirty(); } }

    /// @brief Get uniform values for GPU
    ShapeUniforms getUniforms() const {
        ShapeUniforms uniforms = {};
        uniforms.shapeType = static_cast<int>(m_type);
        uniforms.sizeX = size.x();
        uniforms.sizeY = size.y();
        uniforms.posX = position.x();
        uniforms.posY = position.y();
        uniforms.rotation = rotation;
        uniforms.sides = sides;
        uniforms.cornerRadius = cornerRadius;
        uniforms.thickness = thickness;
        uniforms.softness = softness;
        uniforms.colorR = color.r();
        uniforms.colorG = color.g();
        uniforms.colorB = color.b();
        uniforms.colorA = color.a();
        uniforms.aspect = static_cast<float>(m_width) / m_height;
        return uniforms;
    }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "Shape"; }
    const char* fragmentShader() const override;

    /// @}

private:
    ShapeType m_type = ShapeType::Circle;
};

} // namespace vivid::effects
