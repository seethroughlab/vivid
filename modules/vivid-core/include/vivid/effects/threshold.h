#pragma once

/**
 * @file threshold.h
 * @brief Binary threshold operator
 *
 * Converts input texture to black/white based on luminance threshold.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>
#include <vivid/operator_registry.h>

namespace vivid::effects {

/**
 * @brief Binary thresholding effect
 *
 * Converts the input texture to black and white by comparing pixel luminance
 * against a threshold value. Useful for creating masks, silhouettes, and
 * high-contrast graphics.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | threshold | float | 0-1 | 0.5 | Luminance cutoff point |
 * | softness | float | 0-1 | 0.0 | Edge blend width (0 = hard edge) |
 * | invert | float | 0-1 | 0.0 | Invert output (0 = normal, 1 = inverted) |
 *
 * @par Example
 * @code
 * auto& thresh = chain.add<Threshold>("thresh");
 * thresh.input(&source);
 * thresh.threshold = 0.5f;    // Mid-gray cutoff
 * thresh.softness = 0.05f;    // Slight edge softening
 * thresh.invert = 0.0f;       // Normal (bright = white)
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Black and white texture (preserves alpha)
 *
 * @par TouchDesigner Equivalent
 * Threshold TOP
 */
class Threshold : public TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> threshold{"threshold", 0.5f, 0.0f, 1.0f};  ///< Luminance cutoff
    Param<float> softness{"softness", 0.0f, 0.0f, 1.0f};    ///< Edge blend width
    Param<float> invert{"invert", 0.0f, 0.0f, 1.0f};        ///< Invert output

    /// @}
    // -------------------------------------------------------------------------

    Threshold() {
        registerParam(threshold);
        registerParam(softness);
        registerParam(invert);
    }
    ~Threshold() override;

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Threshold"; }

    /// @}

private:
    void createPipeline(Context& ctx);

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;
};

} // namespace vivid::effects
