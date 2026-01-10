#pragma once

/**
 * @file depth_mask.h
 * @brief Mask 2D effects using 3D depth buffer
 *
 * Uses the depth output from Render3D to mask 2D textures,
 * allowing effects to appear only where 3D objects exist or
 * only in empty space.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>
#include <vivid/operator_registry.h>

namespace vivid::render3d {

// Forward declaration
class Render3D;

/**
 * @brief Mask modes for depth-based masking
 */
enum class DepthMaskMode {
    Object,     ///< Effect visible only where 3D objects are (depth < 1)
    Background, ///< Effect visible only in empty space (depth = 1)
    DepthFade   ///< Effect fades based on depth (closer = more visible)
};

/**
 * @brief Mask 2D effects using 3D depth buffer
 *
 * Uses the linear depth output from a Render3D operator to mask
 * a 2D texture. This allows 2D effects to interact with 3D geometry.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | threshold | float | 0-1 | 0.99 | Depth threshold for Object/Background modes |
 * | softness | float | 0-1 | 0.1 | Edge softness for smoother masking |
 * | invert | bool | - | false | Invert the mask |
 *
 * @par Example
 * @code
 * // Make a glow effect that only appears on the 3D object
 * auto& render = chain.add<Render3D>("render");
 * render.setDepthOutput(true);  // Enable depth output!
 *
 * auto& glow = chain.add<Shape>("glow");
 * glow.type = ShapeType::Ellipse;
 * glow.color.set(1.0f, 0.5f, 0.0f, 0.8f);
 *
 * auto& masked = chain.add<DepthMask>("masked");
 * masked.input("glow");           // 2D effect to mask
 * masked.setRender3D(&render);    // Source of depth
 * masked.mode(DepthMaskMode::Object);  // Only where object exists
 *
 * // Composite: background, then render, then masked effect
 * @endcode
 *
 * @note Render3D must have setDepthOutput(true) enabled
 */
class DepthMask : public effects::TextureOperator {
public:
    Param<float> threshold{"threshold", 0.99f, 0.0f, 1.0f};  ///< Depth threshold
    Param<float> softness{"softness", 0.1f, 0.0f, 1.0f};     ///< Edge softness
    Param<bool> invert{"invert", false};                      ///< Invert mask

    DepthMask() {
        registerParam(threshold);
        registerParam(softness);
        registerParam(invert);
    }
    ~DepthMask() override;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /// Set mask mode
    void mode(DepthMaskMode m) {
        if (m_mode != m) {
            m_mode = m;
            markDirty();
        }
    }

    /// Get current mask mode
    DepthMaskMode getMode() const { return m_mode; }

    /// Set the Render3D operator to get depth from
    /// @note Render3D must have setDepthOutput(true)
    void setRender3D(Render3D* render);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "DepthMask"; }

    /// @}

private:
    void createPipeline(Context& ctx);

    DepthMaskMode m_mode = DepthMaskMode::Object;
    Render3D* m_render3d = nullptr;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;
};

} // namespace vivid::render3d
