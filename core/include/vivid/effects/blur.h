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
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> radius{"radius", 5.0f, 0.0f, 50.0f};  ///< Blur radius in pixels
    Param<int> passes{"passes", 1, 1, 10};              ///< Number of blur iterations

    /// @}
    // -------------------------------------------------------------------------

    Blur() {
        registerParam(radius);
        registerParam(passes);
    }
    ~Blur() override;

    /// @brief Set input texture
    void input(TextureOperator* op) { setInput(0, op); }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Blur"; }

    /// @}

private:
    void createPipeline(Context& ctx);
    void updateBindGroups(Context& ctx, WGPUTextureView inView);

    // GPU resources
    WGPURenderPipeline m_pipelineH = nullptr;  // Horizontal pass
    WGPURenderPipeline m_pipelineV = nullptr;  // Vertical pass
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    // Ping-pong textures for multi-pass
    WGPUTexture m_tempTexture = nullptr;
    WGPUTextureView m_tempView = nullptr;

    // Cached bind groups (avoid per-frame recreation)
    WGPUBindGroup m_bindGroupHFirst = nullptr;     // H pass with external input
    WGPUBindGroup m_bindGroupHSubseq = nullptr;    // H pass with output (multi-pass)
    WGPUBindGroup m_bindGroupV = nullptr;          // V pass with temp texture
    WGPUTextureView m_lastInputView = nullptr;     // Track input changes
};

} // namespace vivid::effects
