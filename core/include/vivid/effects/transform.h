#pragma once

/**
 * @file transform.h
 * @brief 2D transformation operator
 *
 * Scale, rotate, and translate textures with configurable pivot point.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief 2D texture transformation
 *
 * Applies scale, rotation, and translation transformations around a
 * configurable pivot point. Useful for repositioning, zooming, and
 * rotating textures.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | scale | vec2 | 0-10 | (1,1) | Scale factor (x, y) |
 * | rotation | float | -2π to 2π | 0.0 | Rotation in radians |
 * | translate | vec2 | -2 to 2 | (0,0) | Translation offset |
 * | pivot | vec2 | 0-1 | (0.5,0.5) | Transform pivot point |
 *
 * @par Example
 * @code
 * chain.add<Transform>("xform")
 *     .input("source")
 *     .scale(1.5f)
 *     .rotate(0.785f)      // 45 degrees
 *     .pivot(0.5f, 0.5f);  // Center pivot
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Transformed texture
 */
class Transform : public TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Vec2Param scale{"scale", 1.0f, 1.0f, 0.0f, 10.0f};         ///< Scale factor (x, y)
    Param<float> rotation{"rotation", 0.0f, -6.28f, 6.28f};    ///< Rotation in radians
    Vec2Param translate{"translate", 0.0f, 0.0f, -2.0f, 2.0f}; ///< Translation offset
    Vec2Param pivot{"pivot", 0.5f, 0.5f, 0.0f, 1.0f};          ///< Transform pivot point

    /// @}
    // -------------------------------------------------------------------------

    Transform() {
        registerParam(scale);
        registerParam(rotation);
        registerParam(translate);
        registerParam(pivot);
    }
    ~Transform() override;

    /// @brief Set input texture
    void input(TextureOperator* op) { setInput(0, op); }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Transform"; }

    /// @}

private:
    void createPipeline(Context& ctx);

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
