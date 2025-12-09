#pragma once

/**
 * @file edge.h
 * @brief Edge detection operator
 *
 * Detects edges using the Sobel operator.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Sobel edge detection
 *
 * Applies Sobel edge detection to highlight edges in the image.
 * Outputs edge intensity as grayscale.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | strength | float | 0-5 | 1.0 | Edge intensity multiplier |
 * | threshold | float | 0-1 | 0.0 | Minimum edge value to show |
 * | invert | bool | - | false | Invert output (white background) |
 *
 * @par Example
 * @code
 * chain.add<Edge>("edges")
 *     .input("source")
 *     .strength(2.0f)
 *     .threshold(0.1f);
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Grayscale edge map
 */
class Edge : public TextureOperator {
public:
    Edge() = default;
    ~Edge() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set input texture
     * @param op Source operator
     * @return Reference for chaining
     */
    Edge& input(TextureOperator* op) { setInput(0, op); return *this; }

    /**
     * @brief Set edge intensity
     * @param s Strength multiplier (0-5, default 1.0)
     * @return Reference for chaining
     */
    Edge& strength(float s) { m_strength = s; return *this; }

    /**
     * @brief Set edge threshold
     * @param t Threshold (0-1, default 0.0). Edges below this are hidden
     * @return Reference for chaining
     */
    Edge& threshold(float t) { m_threshold = t; return *this; }

    /**
     * @brief Invert output colors
     * @param i True for white background, false for black
     * @return Reference for chaining
     */
    Edge& invert(bool i) { m_invert = i; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Edge"; }

    std::vector<ParamDecl> params() override {
        return { m_strength.decl(), m_threshold.decl(), m_invert.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "strength") { out[0] = m_strength; return true; }
        if (name == "threshold") { out[0] = m_threshold; return true; }
        if (name == "invert") { out[0] = m_invert ? 1.0f : 0.0f; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "strength") { m_strength = value[0]; return true; }
        if (name == "threshold") { m_threshold = value[0]; return true; }
        if (name == "invert") { m_invert = value[0] > 0.5f; return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    Param<float> m_strength{"strength", 1.0f, 0.0f, 5.0f};
    Param<float> m_threshold{"threshold", 0.0f, 0.0f, 1.0f};
    Param<bool> m_invert{"invert", false};

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
