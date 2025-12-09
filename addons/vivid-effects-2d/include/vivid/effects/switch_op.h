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
 * chain.add<Switch>("selector")
 *     .input(0, op1)
 *     .input(1, op2)
 *     .input(2, op3)
 *     .index(1)         // Select second input
 *     .blend(0.5f);     // 50% crossfade to adjacent
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
    Switch() = default;
    ~Switch() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set input at index
     * @param index Input slot (0-7)
     * @param op Source operator
     * @return Reference for chaining
     */
    Switch& input(int index, TextureOperator* op) { setInput(index, op); return *this; }

    /**
     * @brief Set selected input index
     * @param i Index (0-7)
     * @return Reference for chaining
     */
    Switch& index(int i) { m_index = i; return *this; }

    /**
     * @brief Set selected input from float (for LFO control)
     * @param f Float value (truncated to int)
     * @return Reference for chaining
     */
    Switch& index(float f) { m_index = static_cast<int>(f); return *this; }

    /**
     * @brief Set crossfade blend amount
     * @param b Blend (0 = hard switch, >0 = crossfade)
     * @return Reference for chaining
     */
    Switch& blend(float b) { m_blend = b; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Switch"; }

    std::vector<ParamDecl> params() override {
        return { m_index.decl(), m_blend.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "index") { out[0] = m_index; return true; }
        if (name == "blend") { out[0] = m_blend; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "index") { m_index = static_cast<int>(value[0]); return true; }
        if (name == "blend") { m_blend = value[0]; return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    Param<int> m_index{"index", 0, 0, 7};
    Param<float> m_blend{"blend", 0.0f, 0.0f, 1.0f};

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
