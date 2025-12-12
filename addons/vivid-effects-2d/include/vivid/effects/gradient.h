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
    Gradient() = default;
    ~Gradient() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set gradient mode
     * @param m Gradient mode (Linear, Radial, Angular, Diamond)
     * @return Reference for chaining
     */
    Gradient& mode(GradientMode m) { if (m_mode != m) { m_mode = m; markDirty(); } return *this; }

    /**
     * @brief Set gradient angle (linear mode)
     * @param a Angle in radians (0-2π)
     * @return Reference for chaining
     */
    Gradient& angle(float a) { if (m_angle != a) { m_angle = a; markDirty(); } return *this; }

    /**
     * @brief Set gradient center point
     * @param x X position (0-1)
     * @param y Y position (0-1)
     * @return Reference for chaining
     */
    Gradient& center(float x, float y) {
        if (m_center.x() != x || m_center.y() != y) {
            m_center.set(x, y);
            markDirty();
        }
        return *this;
    }

    /**
     * @brief Set gradient scale
     * @param s Scale factor (0.1-10, default 1.0)
     * @return Reference for chaining
     */
    Gradient& scale(float s) { if (m_scale != s) { m_scale = s; markDirty(); } return *this; }

    /**
     * @brief Set gradient offset
     * @param o Offset (-1 to 1)
     * @return Reference for chaining
     */
    Gradient& offset(float o) { if (m_offset != o) { m_offset = o; markDirty(); } return *this; }

    /**
     * @brief Set start color
     * @param r Red (0-1)
     * @param g Green (0-1)
     * @param b Blue (0-1)
     * @param a Alpha (0-1, default 1.0)
     * @return Reference for chaining
     */
    Gradient& colorA(float r, float g, float b, float a = 1.0f) {
        if (m_colorA.r() != r || m_colorA.g() != g || m_colorA.b() != b || m_colorA.a() != a) {
            m_colorA.set(r, g, b, a);
            markDirty();
        }
        return *this;
    }

    /**
     * @brief Set end color
     * @param r Red (0-1)
     * @param g Green (0-1)
     * @param b Blue (0-1)
     * @param a Alpha (0-1, default 1.0)
     * @return Reference for chaining
     */
    Gradient& colorB(float r, float g, float b, float a = 1.0f) {
        if (m_colorB.r() != r || m_colorB.g() != g || m_colorB.b() != b || m_colorB.a() != a) {
            m_colorB.set(r, g, b, a);
            markDirty();
        }
        return *this;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Gradient"; }

    std::vector<ParamDecl> params() override {
        return { m_angle.decl(), m_scale.decl(), m_offset.decl(),
                 m_center.decl(), m_colorA.decl(), m_colorB.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "angle") { out[0] = m_angle; return true; }
        if (name == "scale") { out[0] = m_scale; return true; }
        if (name == "offset") { out[0] = m_offset; return true; }
        if (name == "center") { out[0] = m_center.x(); out[1] = m_center.y(); return true; }
        if (name == "colorA") { out[0] = m_colorA.r(); out[1] = m_colorA.g(); out[2] = m_colorA.b(); out[3] = m_colorA.a(); return true; }
        if (name == "colorB") { out[0] = m_colorB.r(); out[1] = m_colorB.g(); out[2] = m_colorB.b(); out[3] = m_colorB.a(); return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "angle") { angle(value[0]); return true; }
        if (name == "scale") { scale(value[0]); return true; }
        if (name == "offset") { offset(value[0]); return true; }
        if (name == "center") { center(value[0], value[1]); return true; }
        if (name == "colorA") { colorA(value[0], value[1], value[2], value[3]); return true; }
        if (name == "colorB") { colorB(value[0], value[1], value[2], value[3]); return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    GradientMode m_mode = GradientMode::Linear;
    Param<float> m_angle{"angle", 0.0f, 0.0f, 6.283f};
    Param<float> m_scale{"scale", 1.0f, 0.1f, 10.0f};
    Param<float> m_offset{"offset", 0.0f, -1.0f, 1.0f};
    Vec2Param m_center{"center", 0.5f, 0.5f, 0.0f, 1.0f};
    ColorParam m_colorA{"colorA", 0.0f, 0.0f, 0.0f, 1.0f};
    ColorParam m_colorB{"colorB", 1.0f, 1.0f, 1.0f, 1.0f};

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
