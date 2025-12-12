#pragma once

/**
 * @file blur.h
 * @brief Gaussian blur operator
 *
 * Separable Gaussian blur with configurable radius and multi-pass support.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Separable Gaussian blur
 *
 * Applies a two-pass separable Gaussian blur for efficient large-radius blurring.
 * Multiple passes can be used for smoother results at the cost of performance.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | radius | float | 0-50 | 5.0 | Blur radius in pixels |
 * | passes | int | 1-10 | 1 | Number of blur iterations |
 *
 * @par Example
 * @code
 * chain.add<Blur>("blur")
 *     .input("source")
 *     .radius(10.0f)
 *     .passes(2);
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Blurred texture
 */
class Blur : public TextureOperator {
public:
    Blur() = default;
    ~Blur() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set input texture
     * @param op Source operator
     * @return Reference for chaining
     */
    Blur& input(TextureOperator* op) { setInput(0, op); return *this; }

    /**
     * @brief Set blur radius
     * @param r Radius in pixels (0-50, default 5.0)
     * @return Reference for chaining
     */
    Blur& radius(float r) {
        if (m_radius != r) { m_radius = r; markDirty(); }
        return *this;
    }

    /**
     * @brief Set number of blur passes
     * @param p Pass count (1-10, default 1)
     * @return Reference for chaining
     */
    Blur& passes(int p) {
        if (m_passes != p) { m_passes = p; markDirty(); }
        return *this;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Blur"; }

    std::vector<ParamDecl> params() override {
        return { m_radius.decl(), m_passes.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "radius") { out[0] = m_radius; return true; }
        if (name == "passes") { out[0] = m_passes; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "radius") { radius(value[0]); return true; }
        if (name == "passes") { passes(static_cast<int>(value[0])); return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    Param<float> m_radius{"radius", 5.0f, 0.0f, 50.0f};
    Param<int> m_passes{"passes", 1, 1, 10};

    // GPU resources
    WGPURenderPipeline m_pipelineH = nullptr;  // Horizontal pass
    WGPURenderPipeline m_pipelineV = nullptr;  // Vertical pass
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    // Ping-pong textures for multi-pass
    WGPUTexture m_tempTexture = nullptr;
    WGPUTextureView m_tempView = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
