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
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> strength{"strength", 0.1f, 0.0f, 1.0f};    ///< Overall displacement strength
    Param<float> strengthX{"strengthX", 1.0f, 0.0f, 2.0f};  ///< X-axis multiplier
    Param<float> strengthY{"strengthY", 1.0f, 0.0f, 2.0f};  ///< Y-axis multiplier

    /// @}
    // -------------------------------------------------------------------------

    Displace() {
        registerParam(strength);
        registerParam(strengthX);
        registerParam(strengthY);
    }
    ~Displace() override;

    /// @brief Set source texture to distort
    void source(TextureOperator* op) { setInput(0, op); }

    /// @brief Set displacement map texture (R=X, G=Y)
    void map(TextureOperator* op) { setInput(1, op); }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Displace"; }

    /// @}

private:
    void createPipeline(Context& ctx);

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
