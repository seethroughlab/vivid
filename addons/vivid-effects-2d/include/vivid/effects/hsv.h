#pragma once

/**
 * @file hsv.h
 * @brief HSV color adjustment operator
 *
 * Adjust hue, saturation, and value (brightness) in HSV color space.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief HSV color adjustment
 *
 * Converts to HSV color space, applies adjustments, and converts back to RGB.
 * Useful for color grading, hue rotation, and desaturation effects.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | hueShift | float | 0-1 | 0.0 | Hue rotation (0-1 = full 360° rotation) |
 * | saturation | float | 0-3 | 1.0 | Saturation multiplier (0 = grayscale) |
 * | value | float | 0-3 | 1.0 | Value/brightness multiplier |
 *
 * @par Example
 * @code
 * auto& hsv = chain.add<HSV>("hsv");
 * hsv.input(&source);
 * hsv.hueShift = 0.5f;      // Shift hue 180°
 * hsv.saturation = 1.2f;    // Boost saturation
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Color-adjusted texture
 */
class HSV : public TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> hueShift{"hueShift", 0.0f, 0.0f, 1.0f};     ///< Hue rotation (0-1 wraps)
    Param<float> saturation{"saturation", 1.0f, 0.0f, 3.0f}; ///< Saturation multiplier
    Param<float> value{"value", 1.0f, 0.0f, 3.0f};           ///< Value/brightness multiplier

    /// @}
    // -------------------------------------------------------------------------

    HSV() {
        registerParam(hueShift);
        registerParam(saturation);
        registerParam(value);
    }
    ~HSV() override;

    /// @brief Set input texture
    HSV& input(TextureOperator* op) { setInput(0, op); return *this; }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "HSV"; }

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
