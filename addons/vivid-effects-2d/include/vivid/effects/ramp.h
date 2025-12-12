#pragma once

/**
 * @file ramp.h
 * @brief Animated HSV color ramp generator
 *
 * Generates animated gradient patterns with HSV color animation.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Ramp shape types
 */
enum class RampType {
    Linear,      ///< Left to right gradient
    Radial,      ///< Circular from center outward
    Angular,     ///< Conical sweep around center
    Diamond      ///< Diamond-shaped pattern
};

/**
 * @brief Animated HSV color ramp generator
 *
 * Generates animated gradient patterns using HSV color space.
 * The hue continuously animates for rainbow-like effects.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | angle | float | 0-2π | 0.0 | Gradient angle (linear mode) |
 * | scale | float | 0.1-10 | 1.0 | Pattern scale |
 * | repeat | float | 1-10 | 1.0 | Pattern repetition count |
 * | offset | vec2 | - | (0,0) | Pattern offset |
 * | hueOffset | float | 0-1 | 0.0 | Starting hue offset |
 * | hueSpeed | float | 0-2 | 0.5 | Hue animation speed |
 * | hueRange | float | 0-1 | 1.0 | Range of hue variation |
 * | saturation | float | 0-1 | 1.0 | Color saturation |
 * | brightness | float | 0-1 | 1.0 | Color brightness |
 *
 * @par Example
 * @code
 * chain.add<Ramp>("rainbow")
 *     .type(RampType::Radial)
 *     .hueSpeed(0.2f)
 *     .saturation(0.8f);
 * @endcode
 *
 * @par Inputs
 * None (generator)
 *
 * @par Output
 * Animated HSV gradient texture
 */
class Ramp : public TextureOperator {
public:
    Ramp() = default;
    ~Ramp() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set ramp type
     * @param t Ramp type (Linear, Radial, Angular, Diamond)
     * @return Reference for chaining
     */
    Ramp& type(RampType t) { if (m_type != t) { m_type = t; markDirty(); } return *this; }

    /**
     * @brief Set gradient angle (linear mode)
     * @param a Angle in radians
     * @return Reference for chaining
     */
    Ramp& angle(float a) { if (m_angle != a) { m_angle = a; markDirty(); } return *this; }

    /**
     * @brief Set pattern offset
     * @param x X offset
     * @param y Y offset
     * @return Reference for chaining
     */
    Ramp& offset(float x, float y) {
        if (m_offset.x() != x || m_offset.y() != y) {
            m_offset.set(x, y);
            markDirty();
        }
        return *this;
    }

    /**
     * @brief Set pattern scale
     * @param s Scale factor (0.1-10)
     * @return Reference for chaining
     */
    Ramp& scale(float s) { if (m_scale != s) { m_scale = s; markDirty(); } return *this; }

    /**
     * @brief Set pattern repetition
     * @param r Repeat count (1-10)
     * @return Reference for chaining
     */
    Ramp& repeat(float r) { if (m_repeat != r) { m_repeat = r; markDirty(); } return *this; }

    /**
     * @brief Set starting hue offset
     * @param h Hue offset (0-1)
     * @return Reference for chaining
     */
    Ramp& hueOffset(float h) { if (m_hueOffset != h) { m_hueOffset = h; markDirty(); } return *this; }

    /**
     * @brief Set hue animation speed
     * @param s Speed (0-2, default 0.5)
     * @return Reference for chaining
     */
    Ramp& hueSpeed(float s) { if (m_hueSpeed != s) { m_hueSpeed = s; markDirty(); } return *this; }

    /**
     * @brief Set hue variation range
     * @param r Range (0-1, default 1.0 = full rainbow)
     * @return Reference for chaining
     */
    Ramp& hueRange(float r) { if (m_hueRange != r) { m_hueRange = r; markDirty(); } return *this; }

    /**
     * @brief Set color saturation
     * @param s Saturation (0-1, default 1.0)
     * @return Reference for chaining
     */
    Ramp& saturation(float s) { if (m_saturation != s) { m_saturation = s; markDirty(); } return *this; }

    /**
     * @brief Set color brightness
     * @param b Brightness (0-1, default 1.0)
     * @return Reference for chaining
     */
    Ramp& brightness(float b) { if (m_brightness != b) { m_brightness = b; markDirty(); } return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Ramp"; }

    std::vector<ParamDecl> params() override {
        return { m_angle.decl(), m_scale.decl(), m_repeat.decl(), m_hueOffset.decl(),
                 m_hueSpeed.decl(), m_hueRange.decl(), m_saturation.decl(),
                 m_brightness.decl(), m_offset.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "angle") { out[0] = m_angle; return true; }
        if (name == "scale") { out[0] = m_scale; return true; }
        if (name == "repeat") { out[0] = m_repeat; return true; }
        if (name == "hueOffset") { out[0] = m_hueOffset; return true; }
        if (name == "hueSpeed") { out[0] = m_hueSpeed; return true; }
        if (name == "hueRange") { out[0] = m_hueRange; return true; }
        if (name == "saturation") { out[0] = m_saturation; return true; }
        if (name == "brightness") { out[0] = m_brightness; return true; }
        if (name == "offset") { out[0] = m_offset.x(); out[1] = m_offset.y(); return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "angle") { angle(value[0]); return true; }
        if (name == "scale") { scale(value[0]); return true; }
        if (name == "repeat") { repeat(value[0]); return true; }
        if (name == "hueOffset") { hueOffset(value[0]); return true; }
        if (name == "hueSpeed") { hueSpeed(value[0]); return true; }
        if (name == "hueRange") { hueRange(value[0]); return true; }
        if (name == "saturation") { saturation(value[0]); return true; }
        if (name == "brightness") { brightness(value[0]); return true; }
        if (name == "offset") { offset(value[0], value[1]); return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    // Parameters
    RampType m_type = RampType::Linear;
    Param<float> m_angle{"angle", 0.0f, 0.0f, 6.283f};
    Param<float> m_scale{"scale", 1.0f, 0.1f, 10.0f};
    Param<float> m_repeat{"repeat", 1.0f, 1.0f, 10.0f};
    Vec2Param m_offset{"offset", 0.0f, 0.0f};

    // HSV parameters
    Param<float> m_hueOffset{"hueOffset", 0.0f, 0.0f, 1.0f};
    Param<float> m_hueSpeed{"hueSpeed", 0.5f, 0.0f, 2.0f};
    Param<float> m_hueRange{"hueRange", 1.0f, 0.0f, 1.0f};
    Param<float> m_saturation{"saturation", 1.0f, 0.0f, 1.0f};
    Param<float> m_brightness{"brightness", 1.0f, 0.0f, 1.0f};

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
