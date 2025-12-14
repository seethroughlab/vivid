#pragma once

/**
 * @file switch_op.h
 * @brief Input selector/switcher operator
 *
 * Selects between multiple texture inputs by index.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Input selector/switcher
 *
 * Selects between multiple texture inputs by index. Supports up to
 * 8 inputs with optional crossfade blending between adjacent inputs.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | index | int | 0-7 | 0 | Selected input index |
 * | blend | float | 0-1 | 0.0 | Crossfade amount (0 = hard switch) |
 *
 * @par Example
 * @code
 * auto& sw = chain.add<Switch>("selector");
 * sw.input(0, op1);
 * sw.input(1, op2);
 * sw.input(2, op3);
 * sw.index = 1;       // Select second input
 * sw.blend = 0.5f;    // 50% crossfade to adjacent
 * @endcode
 *
 * @par Inputs
 * - Input 0-7: Texture inputs to select from
 *
 * @par Output
 * Selected (and optionally blended) texture
 */
class Switch : public TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<int> index{"index", 0, 0, 7};          ///< Selected input index (0-7)
    Param<float> blend{"blend", 0.0f, 0.0f, 1.0f}; ///< Crossfade blend amount

    /// @}
    // -------------------------------------------------------------------------

    Switch() {
        registerParam(index);
        registerParam(blend);
    }
    ~Switch() override;

    /// @brief Set input at index
    Switch& input(int idx, TextureOperator* op) { setInput(idx, op); return *this; }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Switch"; }

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
