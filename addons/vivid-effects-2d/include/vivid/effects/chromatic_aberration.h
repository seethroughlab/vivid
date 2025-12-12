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
    ChromaticAberration() = default;
    ~ChromaticAberration() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set input texture
     * @param op Source operator
     * @return Reference for chaining
     */
    ChromaticAberration& input(TextureOperator* op) { setInput(0, op); return *this; }

    /**
     * @brief Set separation amount
     * @param a Amount (0-0.1, default 0.01)
     * @return Reference for chaining
     */
    ChromaticAberration& amount(float a) { if (m_amount != a) { m_amount = a; markDirty(); } return *this; }

    /**
     * @brief Set separation angle (linear mode)
     * @param a Angle in radians
     * @return Reference for chaining
     */
    ChromaticAberration& angle(float a) { if (m_angle != a) { m_angle = a; markDirty(); } return *this; }

    /**
     * @brief Enable radial separation mode
     * @param r True for radial, false for linear
     * @return Reference for chaining
     */
    ChromaticAberration& radial(bool r) { if (m_radial != r) { m_radial = r; markDirty(); } return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "ChromaticAberration"; }

    std::vector<ParamDecl> params() override {
        return { m_amount.decl(), m_angle.decl(), m_radial.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "amount") { out[0] = m_amount; return true; }
        if (name == "angle") { out[0] = m_angle; return true; }
        if (name == "radial") { out[0] = m_radial ? 1.0f : 0.0f; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "amount") { amount(value[0]); return true; }
        if (name == "angle") { angle(value[0]); return true; }
        if (name == "radial") { radial(value[0] > 0.5f); return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    Param<float> m_amount{"amount", 0.01f, 0.0f, 0.1f};
    Param<float> m_angle{"angle", 0.0f, -6.28f, 6.28f};
    Param<bool> m_radial{"radial", true};

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
