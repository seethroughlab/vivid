#pragma once

/**
 * @file switch_op.h
 * @brief Input selector/switcher operator
 *
 * Selects between multiple texture inputs by index.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>
#include <vivid/operator_registry.h>

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
    /// @name Self-Description
    /// @{

    static OperatorDescriptor describe() {
        return OperatorDescriptor("Switch", "Compositing", "Switch between inputs")
            .requireInput()
            .withInputs({
                {"input", "Add inputs with input(index, \"name\")", true}
            })
            .withUsage(
                "auto& sw = chain.add<Switch>(\"switch\");\n"
                "sw.input(0, \"option_a\");\n"
                "sw.input(1, \"option_b\");\n"
                "sw.input(2, \"option_c\");\n"
                "sw.index = 0;    // Which input to output (0-indexed)\n"
                "sw.blend = 0.0f; // Crossfade between adjacent inputs\n"
            )
            .withExamples({{"modules/vivid-core/examples/conditional-routing"}});
    }

    /// @}
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

    /// @brief Set input at index by name
    void input(int idx, const std::string& name) { setInputByName(idx, name); }

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
};

} // namespace vivid::effects
