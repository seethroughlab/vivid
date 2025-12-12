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
    BarrelDistortion() = default;
    ~BarrelDistortion() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    BarrelDistortion& input(TextureOperator* op) { setInput(0, op); return *this; }

    /**
     * @brief Set curvature amount
     * @param c Curvature (0-1, default 0.1). Higher = more curved edges
     */
    BarrelDistortion& curvature(float c) { if (m_curvature != c) { m_curvature = c; markDirty(); } return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "BarrelDistortion"; }

    std::vector<ParamDecl> params() override { return { m_curvature.decl() }; }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "curvature") { out[0] = m_curvature; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "curvature") { curvature(value[0]); return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    Param<float> m_curvature{"curvature", 0.1f, 0.0f, 1.0f};

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;
    bool m_initialized = false;
};

} // namespace vivid::effects
