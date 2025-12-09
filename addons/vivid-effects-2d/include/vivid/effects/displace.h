#pragma once

/**
 * @file displace.h
 * @brief Displacement mapping operator
 *
 * Distorts one texture using another as a displacement map.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Displacement mapping effect
 *
 * Uses a second texture as a displacement map to distort the source image.
 * The displacement map's red channel controls X offset, green controls Y.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | strength | float | 0-1 | 0.1 | Overall displacement strength |
 * | strengthX | float | 0-2 | 1.0 | X-axis strength multiplier |
 * | strengthY | float | 0-2 | 1.0 | Y-axis strength multiplier |
 *
 * @par Example
 * @code
 * chain.add<Noise>("disp_map").scale(2.0f);
 * chain.add<Displace>("distort")
 *     .source("image")
 *     .map("disp_map")
 *     .strength(0.05f);
 * @endcode
 *
 * @par Inputs
 * - source: Texture to distort
 * - map: Displacement map (R=X, G=Y offset)
 *
 * @par Output
 * Distorted texture
 */
class Displace : public TextureOperator {
public:
    Displace() = default;
    ~Displace() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set source texture to distort
     * @param op Source operator
     * @return Reference for chaining
     */
    Displace& source(TextureOperator* op) { setInput(0, op); return *this; }

    /**
     * @brief Set displacement map texture
     * @param op Displacement map operator (R=X, G=Y)
     * @return Reference for chaining
     */
    Displace& map(TextureOperator* op) { setInput(1, op); return *this; }

    /**
     * @brief Set overall displacement strength
     * @param s Strength (0-1, default 0.1)
     * @return Reference for chaining
     */
    Displace& strength(float s) { m_strength = s; return *this; }

    /**
     * @brief Set X-axis displacement multiplier
     * @param s X strength (0-2, default 1.0)
     * @return Reference for chaining
     */
    Displace& strengthX(float s) { m_strengthX = s; return *this; }

    /**
     * @brief Set Y-axis displacement multiplier
     * @param s Y strength (0-2, default 1.0)
     * @return Reference for chaining
     */
    Displace& strengthY(float s) { m_strengthY = s; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Displace"; }

    std::vector<ParamDecl> params() override {
        return { m_strength.decl(), m_strengthX.decl(), m_strengthY.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "strength") { out[0] = m_strength; return true; }
        if (name == "strengthX") { out[0] = m_strengthX; return true; }
        if (name == "strengthY") { out[0] = m_strengthY; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "strength") { m_strength = value[0]; return true; }
        if (name == "strengthX") { m_strengthX = value[0]; return true; }
        if (name == "strengthY") { m_strengthY = value[0]; return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    Param<float> m_strength{"strength", 0.1f, 0.0f, 1.0f};
    Param<float> m_strengthX{"strengthX", 1.0f, 0.0f, 2.0f};
    Param<float> m_strengthY{"strengthY", 1.0f, 0.0f, 2.0f};

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
