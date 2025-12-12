#pragma once

/**
 * @file vignette.h
 * @brief Edge darkening vignette effect
 *
 * Darkens the edges and corners of the image, simulating the
 * natural light falloff of camera lenses.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Edge darkening vignette effect
 *
 * Creates a gradual darkening from the center to the edges of the image,
 * simulating the light falloff seen in camera lenses and CRT monitors.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | intensity | float | 0-2 | 0.5 | Darkening strength |
 * | softness | float | 0-2 | 0.5 | Edge softness/gradient width |
 * | roundness | float | 0-1 | 1.0 | Shape: 0 = rectangular, 1 = circular |
 *
 * @par Example
 * @code
 * chain.add<Vignette>("vignette")
 *     .input("source")
 *     .intensity(0.4f)
 *     .softness(0.6f);
 * @endcode
 */
class Vignette : public TextureOperator {
public:
    Vignette() = default;
    ~Vignette() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    Vignette& input(TextureOperator* op) { setInput(0, op); return *this; }

    /**
     * @brief Set vignette intensity
     * @param i Intensity (0-2, default 0.5). Higher = darker edges
     */
    Vignette& intensity(float i) { if (m_intensity != i) { m_intensity = i; markDirty(); } return *this; }

    /**
     * @brief Set edge softness
     * @param s Softness (0-2, default 0.5). Higher = wider gradient
     */
    Vignette& softness(float s) { if (m_softness != s) { m_softness = s; markDirty(); } return *this; }

    /**
     * @brief Set shape roundness
     * @param r Roundness (0-1, default 1.0). 0 = rectangular, 1 = circular
     */
    Vignette& roundness(float r) { if (m_roundness != r) { m_roundness = r; markDirty(); } return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Vignette"; }

    std::vector<ParamDecl> params() override {
        return { m_intensity.decl(), m_softness.decl(), m_roundness.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "intensity") { out[0] = m_intensity; return true; }
        if (name == "softness") { out[0] = m_softness; return true; }
        if (name == "roundness") { out[0] = m_roundness; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "intensity") { intensity(value[0]); return true; }
        if (name == "softness") { softness(value[0]); return true; }
        if (name == "roundness") { roundness(value[0]); return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    Param<float> m_intensity{"intensity", 0.5f, 0.0f, 2.0f};
    Param<float> m_softness{"softness", 0.5f, 0.0f, 2.0f};
    Param<float> m_roundness{"roundness", 1.0f, 0.0f, 1.0f};

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;
    bool m_initialized = false;
};

} // namespace vivid::effects
