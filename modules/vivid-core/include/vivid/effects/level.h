#pragma once

/**
 * @file level.h
 * @brief Input/output level adjustment operator
 *
 * Full color correction with input range, gamma, and output range.
 * More powerful than Brightness for precise level adjustments.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>
#include <vivid/operator_registry.h>

namespace vivid::effects {

/**
 * @brief Input/output level adjustment effect
 *
 * Remaps input color values through black/white points and gamma correction.
 * Similar to Levels in Photoshop or Level TOP in TouchDesigner.
 *
 * The transformation is:
 * 1. Normalize: `(input - inBlack) / (inWhite - inBlack)`
 * 2. Gamma: `pow(normalized, 1/gamma)`
 * 3. Output range: `result * (outWhite - outBlack) + outBlack`
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | inBlack | float | 0-1 | 0.0 | Input black point (values below become 0) |
 * | inWhite | float | 0-1 | 1.0 | Input white point (values above become 1) |
 * | gamma | float | 0.1-10 | 1.0 | Gamma correction (>1 darkens, <1 brightens) |
 * | outBlack | float | 0-1 | 0.0 | Output black level |
 * | outWhite | float | 0-1 | 1.0 | Output white level |
 *
 * @par Example
 * @code
 * auto& level = chain.add<Level>("level");
 * level.input(&source);
 * level.inBlack = 0.1f;   // Crush shadows
 * level.inWhite = 0.9f;   // Clip highlights
 * level.gamma = 1.2f;     // Darken midtones
 * level.outBlack = 0.05f; // Lift shadows slightly
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Level-adjusted texture (preserves alpha)
 *
 * @par TouchDesigner Equivalent
 * Level TOP
 */
class Level : public TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> inBlack{"inBlack", 0.0f, 0.0f, 1.0f};    ///< Input black point
    Param<float> inWhite{"inWhite", 1.0f, 0.0f, 1.0f};    ///< Input white point
    Param<float> gamma{"gamma", 1.0f, 0.1f, 10.0f};       ///< Gamma correction
    Param<float> outBlack{"outBlack", 0.0f, 0.0f, 1.0f};  ///< Output black level
    Param<float> outWhite{"outWhite", 1.0f, 0.0f, 1.0f};  ///< Output white level

    /// @}
    // -------------------------------------------------------------------------

    Level() {
        registerParam(inBlack);
        registerParam(inWhite);
        registerParam(gamma);
        registerParam(outBlack);
        registerParam(outWhite);
    }
    ~Level() override;

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Level"; }

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
