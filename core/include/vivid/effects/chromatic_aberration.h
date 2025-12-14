#pragma once

/**
 * @file chromatic_aberration.h
 * @brief RGB channel separation effect
 *
 * Simulates lens chromatic aberration by offsetting color channels.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief RGB channel separation effect
 *
 * Offsets the red, green, and blue color channels to create a
 * chromatic aberration effect. Supports both linear and radial modes.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | amount | float | 0-0.1 | 0.01 | Separation distance |
 * | angle | float | -2π to 2π | 0.0 | Direction angle (linear mode) |
 * | radial | bool | - | true | Use radial vs linear separation |
 *
 * @par Example
 * @code
 * chain.add<ChromaticAberration>("aberration")
 *     .input("source")
 *     .amount(0.02f)
 *     .radial(true);
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Texture with RGB channel separation
 */
class ChromaticAberration : public TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> amount{"amount", 0.01f, 0.0f, 0.1f};  ///< Separation amount
    Param<float> angle{"angle", 0.0f, -6.28f, 6.28f};  ///< Direction angle (linear mode)
    Param<bool> radial{"radial", true};                 ///< Radial vs linear mode

    /// @}
    // -------------------------------------------------------------------------

    ChromaticAberration() {
        registerParam(amount);
        registerParam(angle);
        registerParam(radial);
    }
    ~ChromaticAberration() override;

    /// @brief Set input texture
    void input(TextureOperator* op) { setInput(0, op); }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "ChromaticAberration"; }

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
