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
 * chain.add<HSV>("hsv")
 *     .input("source")
 *     .hueShift(0.5f)      // Shift hue 180°
 *     .saturation(1.2f);   // Boost saturation
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
    HSV() = default;
    ~HSV() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set input texture
     * @param op Source operator
     * @return Reference for chaining
     */
    HSV& input(TextureOperator* op) { setInput(0, op); return *this; }

    /**
     * @brief Set hue shift
     * @param h Hue rotation (0-1 = full 360° rotation, default 0)
     * @return Reference for chaining
     */
    HSV& hueShift(float h) { m_hueShift = h; return *this; }

    /**
     * @brief Set saturation multiplier
     * @param s Saturation (0 = grayscale, 1 = normal, >1 = oversaturated)
     * @return Reference for chaining
     */
    HSV& saturation(float s) { m_saturation = s; return *this; }

    /**
     * @brief Set value/brightness multiplier
     * @param v Value multiplier (0-3, default 1.0)
     * @return Reference for chaining
     */
    HSV& value(float v) { m_value = v; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "HSV"; }

    std::vector<ParamDecl> params() override {
        return { m_hueShift.decl(), m_saturation.decl(), m_value.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "hueShift") { out[0] = m_hueShift; return true; }
        if (name == "saturation") { out[0] = m_saturation; return true; }
        if (name == "value") { out[0] = m_value; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "hueShift") { m_hueShift = value[0]; return true; }
        if (name == "saturation") { m_saturation = value[0]; return true; }
        if (name == "value") { m_value = value[0]; return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    Param<float> m_hueShift{"hueShift", 0.0f, 0.0f, 1.0f};
    Param<float> m_saturation{"saturation", 1.0f, 0.0f, 3.0f};
    Param<float> m_value{"value", 1.0f, 0.0f, 3.0f};

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
