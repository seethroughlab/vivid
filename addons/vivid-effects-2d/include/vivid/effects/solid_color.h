#pragma once

/**
 * @file solid_color.h
 * @brief Solid color generator
 *
 * Generates a uniform solid color texture.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Solid color generator
 *
 * Generates a texture filled with a single uniform color.
 * Useful as a background or for masking operations.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | color | color | - | black | Fill color (RGBA) |
 *
 * @par Example
 * @code
 * chain.add<SolidColor>("bg")
 *     .color(0.1f, 0.1f, 0.2f);  // Dark blue background
 * @endcode
 *
 * @par Inputs
 * None (generator)
 *
 * @par Output
 * Solid color texture
 */
class SolidColor : public TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    ColorParam color{"color", 0.0f, 0.0f, 0.0f, 1.0f}; ///< Fill color (RGBA)

    /// @}
    // -------------------------------------------------------------------------

    SolidColor() {
        registerParam(color);
    }
    ~SolidColor() override;

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "SolidColor"; }

    /// @}

private:
    void createPipeline(Context& ctx);

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
