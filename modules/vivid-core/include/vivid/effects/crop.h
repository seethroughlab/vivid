#pragma once

/**
 * @file crop.h
 * @brief Region extraction operator
 *
 * Extracts a rectangular region from the input texture.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>
#include <vivid/operator_registry.h>

namespace vivid::effects {

/**
 * @brief Crop region extraction effect
 *
 * Extracts a rectangular portion of the input texture. The output texture
 * is resized to match the cropped region dimensions.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | left | float | 0-1 | 0.0 | Left edge (normalized, 0 = leftmost) |
 * | right | float | 0-1 | 1.0 | Right edge (normalized, 1 = rightmost) |
 * | top | float | 0-1 | 0.0 | Top edge (normalized, 0 = topmost) |
 * | bottom | float | 0-1 | 1.0 | Bottom edge (normalized, 1 = bottommost) |
 *
 * @par Example
 * @code
 * auto& crop = chain.add<Crop>("crop");
 * crop.input(&source);
 * crop.left = 0.25f;    // Crop 25% from left
 * crop.right = 0.75f;   // Crop 25% from right
 * crop.top = 0.1f;      // Crop 10% from top
 * crop.bottom = 0.9f;   // Crop 10% from bottom
 * // Output will be 50% width, 80% height of input
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Cropped texture at reduced resolution
 *
 * @par TouchDesigner Equivalent
 * Crop TOP
 */
class Crop : public TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> left{"left", 0.0f, 0.0f, 1.0f};      ///< Left edge (normalized)
    Param<float> right{"right", 1.0f, 0.0f, 1.0f};    ///< Right edge (normalized)
    Param<float> top{"top", 0.0f, 0.0f, 1.0f};        ///< Top edge (normalized)
    Param<float> bottom{"bottom", 1.0f, 0.0f, 1.0f};  ///< Bottom edge (normalized)

    /// @}
    // -------------------------------------------------------------------------

    Crop() {
        registerParam(left);
        registerParam(right);
        registerParam(top);
        registerParam(bottom);
    }
    ~Crop() override;

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Crop"; }

    /// @}

private:
    void createPipeline(Context& ctx);
    void updateOutputSize(Context& ctx);

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    // Cached input dimensions for resize detection
    int m_lastInputWidth = 0;
    int m_lastInputHeight = 0;
};

} // namespace vivid::effects
