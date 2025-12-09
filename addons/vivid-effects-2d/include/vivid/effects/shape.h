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
    Shape() = default;
    ~Shape() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set shape type
     * @param t Shape type
     * @return Reference for chaining
     */
    Shape& type(ShapeType t) { m_type = t; return *this; }

    /**
     * @brief Set uniform shape size
     * @param s Size (applies to both dimensions)
     * @return Reference for chaining
     */
    Shape& size(float s) { m_size.set(s, s); return *this; }

    /**
     * @brief Set non-uniform shape size
     * @param x Width
     * @param y Height
     * @return Reference for chaining
     */
    Shape& size(float x, float y) { m_size.set(x, y); return *this; }

    /**
     * @brief Set shape position
     * @param x X position (0-1)
     * @param y Y position (0-1)
     * @return Reference for chaining
     */
    Shape& position(float x, float y) { m_position.set(x, y); return *this; }

    /**
     * @brief Set rotation angle
     * @param r Rotation in radians
     * @return Reference for chaining
     */
    Shape& rotation(float r) { m_rotation = r; return *this; }

    /**
     * @brief Set polygon/star side count
     * @param n Number of sides (3-32)
     * @return Reference for chaining
     */
    Shape& sides(int n) { m_sides = n; return *this; }

    /**
     * @brief Set corner radius for rounded shapes
     * @param r Corner radius (0-0.5)
     * @return Reference for chaining
     */
    Shape& cornerRadius(float r) { m_cornerRadius = r; return *this; }

    /**
     * @brief Set ring/outline thickness
     * @param t Thickness (0-0.5)
     * @return Reference for chaining
     */
    Shape& thickness(float t) { m_thickness = t; return *this; }

    /**
     * @brief Set edge softness
     * @param s Softness (0-0.2, default 0.01)
     * @return Reference for chaining
     */
    Shape& softness(float s) { m_softness = s; return *this; }

    /**
     * @brief Set shape color
     * @param r Red (0-1)
     * @param g Green (0-1)
     * @param b Blue (0-1)
     * @param a Alpha (0-1, default 1.0)
     * @return Reference for chaining
     */
    Shape& color(float r, float g, float b, float a = 1.0f) {
        m_color.set(r, g, b, a); return *this;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Shape"; }

    std::vector<ParamDecl> params() override {
        return { m_size.decl(), m_position.decl(), m_rotation.decl(), m_sides.decl(),
                 m_cornerRadius.decl(), m_thickness.decl(), m_softness.decl(), m_color.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "size") { out[0] = m_size.x(); out[1] = m_size.y(); return true; }
        if (name == "position") { out[0] = m_position.x(); out[1] = m_position.y(); return true; }
        if (name == "rotation") { out[0] = m_rotation; return true; }
        if (name == "sides") { out[0] = m_sides; return true; }
        if (name == "cornerRadius") { out[0] = m_cornerRadius; return true; }
        if (name == "thickness") { out[0] = m_thickness; return true; }
        if (name == "softness") { out[0] = m_softness; return true; }
        if (name == "color") { out[0] = m_color.r(); out[1] = m_color.g(); out[2] = m_color.b(); out[3] = m_color.a(); return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "size") { m_size.set(value[0], value[1]); return true; }
        if (name == "position") { m_position.set(value[0], value[1]); return true; }
        if (name == "rotation") { m_rotation = value[0]; return true; }
        if (name == "sides") { m_sides = static_cast<int>(value[0]); return true; }
        if (name == "cornerRadius") { m_cornerRadius = value[0]; return true; }
        if (name == "thickness") { m_thickness = value[0]; return true; }
        if (name == "softness") { m_softness = value[0]; return true; }
        if (name == "color") { m_color.set(value[0], value[1], value[2], value[3]); return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    ShapeType m_type = ShapeType::Circle;
    Vec2Param m_size{"size", 0.5f, 0.5f, 0.0f, 2.0f};
    Vec2Param m_position{"position", 0.5f, 0.5f, 0.0f, 1.0f};
    Param<float> m_rotation{"rotation", 0.0f, -6.28f, 6.28f};
    Param<int> m_sides{"sides", 5, 3, 32};
    Param<float> m_cornerRadius{"cornerRadius", 0.0f, 0.0f, 0.5f};
    Param<float> m_thickness{"thickness", 0.1f, 0.0f, 0.5f};
    Param<float> m_softness{"softness", 0.01f, 0.0f, 0.2f};
    ColorParam m_color{"color", 1.0f, 1.0f, 1.0f, 1.0f};

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
