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
 * auto& edges = chain.add<Edge>("edges");
 * edges.input(&source);
 * edges.strength = 2.0f;
 * edges.threshold = 0.1f;
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
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> strength{"strength", 1.0f, 0.0f, 5.0f};   ///< Edge intensity
    Param<float> threshold{"threshold", 0.0f, 0.0f, 1.0f}; ///< Minimum edge value
    Param<bool> invert{"invert", false};                    ///< Invert output

    /// @}
    // -------------------------------------------------------------------------

    Edge() {
        registerParam(strength);
        registerParam(threshold);
        registerParam(invert);
    }
    ~Edge() override;

    /// @brief Set input texture
    Edge& input(TextureOperator* op) { setInput(0, op); return *this; }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Edge"; }

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
