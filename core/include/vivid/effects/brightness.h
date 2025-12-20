#pragma once

/**
 * @file brightness.h
 * @brief Brightness, contrast, and gamma adjustment operator
 *
 * Adjusts brightness, contrast, and gamma correction of input textures.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Brightness, contrast, and gamma adjustment
 *
 * Applies standard color correction operations: brightness offset,
 * contrast scaling around mid-gray, and gamma correction.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | brightness | float | -1 to 1 | 0.0 | Brightness offset |
 * | contrast | float | 0-3 | 1.0 | Contrast multiplier (0 = flat gray) |
 * | gamma | float | 0.1-3 | 1.0 | Gamma correction exponent |
 *
 * @par Example
 * @code
 * auto& levels = chain.add<Brightness>("levels");
 * levels.input(&source);
 * levels.brightness = 0.1f;
 * levels.contrast = 1.2f;
 * levels.gamma = 0.9f;
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Color-corrected texture
 */
class Brightness : public TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> brightness{"brightness", 0.0f, -1.0f, 1.0f}; ///< Brightness offset
    Param<float> contrast{"contrast", 1.0f, 0.0f, 3.0f};      ///< Contrast multiplier
    Param<float> gamma{"gamma", 1.0f, 0.1f, 3.0f};            ///< Gamma correction

    /// @}
    // -------------------------------------------------------------------------

    Brightness() {
        registerParam(brightness);
        registerParam(contrast);
        registerParam(gamma);
    }
    ~Brightness() override;

    /// @brief Set input texture
    void input(TextureOperator* op) { setInput(0, op); }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Brightness"; }

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
