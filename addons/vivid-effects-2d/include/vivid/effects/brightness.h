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
 * chain.add<Brightness>("levels")
 *     .input("source")
 *     .brightness(0.1f)
 *     .contrast(1.2f)
 *     .gamma(0.9f);
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
    Brightness() = default;
    ~Brightness() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set input texture
     * @param op Source operator
     * @return Reference for chaining
     */
    Brightness& input(TextureOperator* op) { setInput(0, op); return *this; }

    /**
     * @brief Set brightness offset
     * @param b Brightness (-1 to 1, default 0)
     * @return Reference for chaining
     */
    Brightness& brightness(float b) { m_brightness = b; return *this; }

    /**
     * @brief Set contrast multiplier
     * @param c Contrast (0 = flat gray, 1 = normal, >1 = high contrast)
     * @return Reference for chaining
     */
    Brightness& contrast(float c) { m_contrast = c; return *this; }

    /**
     * @brief Set gamma correction
     * @param g Gamma exponent (0.1-3, default 1.0)
     * @return Reference for chaining
     */
    Brightness& gamma(float g) { m_gamma = g; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Brightness"; }

    std::vector<ParamDecl> params() override {
        return { m_brightness.decl(), m_contrast.decl(), m_gamma.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "brightness") { out[0] = m_brightness; return true; }
        if (name == "contrast") { out[0] = m_contrast; return true; }
        if (name == "gamma") { out[0] = m_gamma; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "brightness") { m_brightness = value[0]; return true; }
        if (name == "contrast") { m_contrast = value[0]; return true; }
        if (name == "gamma") { m_gamma = value[0]; return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    Param<float> m_brightness{"brightness", 0.0f, -1.0f, 1.0f};
    Param<float> m_contrast{"contrast", 1.0f, 0.0f, 3.0f};
    Param<float> m_gamma{"gamma", 1.0f, 0.1f, 3.0f};

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
