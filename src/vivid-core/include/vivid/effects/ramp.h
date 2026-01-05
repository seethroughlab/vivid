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
 * auto& ramp = chain.add<Ramp>("rainbow");
 * ramp.type(RampType::Radial);
 * ramp.hueSpeed = 0.2f;
 * ramp.saturation = 0.8f;
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
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> angle{"angle", 0.0f, 0.0f, 6.283f};          ///< Gradient angle (linear mode)
    Param<float> scale{"scale", 1.0f, 0.1f, 10.0f};           ///< Pattern scale
    Param<float> repeat{"repeat", 1.0f, 1.0f, 10.0f};         ///< Pattern repetition
    Vec2Param offset{"offset", 0.0f, 0.0f};                    ///< Pattern offset
    Param<float> hueOffset{"hueOffset", 0.0f, 0.0f, 1.0f};    ///< Starting hue offset
    Param<float> hueSpeed{"hueSpeed", 0.5f, 0.0f, 2.0f};      ///< Hue animation speed
    Param<float> hueRange{"hueRange", 1.0f, 0.0f, 1.0f};      ///< Range of hue variation
    Param<float> saturation{"saturation", 1.0f, 0.0f, 1.0f};  ///< Color saturation
    Param<float> brightness{"brightness", 1.0f, 0.0f, 1.0f};  ///< Color brightness

    /// @}
    // -------------------------------------------------------------------------

    Ramp() {
        registerParam(angle);
        registerParam(scale);
        registerParam(repeat);
        registerParam(offset);
        registerParam(hueOffset);
        registerParam(hueSpeed);
        registerParam(hueRange);
        registerParam(saturation);
        registerParam(brightness);
    }
    ~Ramp() override;

    /// @brief Set ramp type (Linear, Radial, Angular, Diamond)
    void type(RampType t) { if (m_type != t) { m_type = t; markDirty(); } }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Ramp"; }

    /// @}

private:
    void createPipeline(Context& ctx);

    RampType m_type = RampType::Linear;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
};

} // namespace vivid::effects
