#pragma once

/**
 * @file barrel_distortion.h
 * @brief CRT-style barrel distortion effect
 *
 * Warps the image to simulate the curved glass of a CRT monitor.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Barrel distortion for CRT curvature simulation
 *
 * Applies a barrel distortion that curves the image edges inward,
 * simulating how CRT monitors had curved glass screens.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | curvature | float | 0-1 | 0.1 | Distortion amount (0 = none, 1 = extreme) |
 *
 * @par Example
 * @code
 * chain.add<BarrelDistortion>("barrel")
 *     .input("source")
 *     .curvature(0.08f);
 * @endcode
 */
class BarrelDistortion : public TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> curvature{"curvature", 0.1f, 0.0f, 1.0f}; ///< Distortion amount

    /// @}
    // -------------------------------------------------------------------------

    BarrelDistortion() {
        registerParam(curvature);
    }
    ~BarrelDistortion() override;

    /// @brief Set input texture
    BarrelDistortion& input(TextureOperator* op) { setInput(0, op); return *this; }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "BarrelDistortion"; }

    /// @}

private:
    void createPipeline(Context& ctx);

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;
    bool m_initialized = false;
};

} // namespace vivid::effects
