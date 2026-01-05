#pragma once

/**
 * @file bloom.h
 * @brief Glow/bloom effect operator
 *
 * Adds a luminous glow effect to bright areas of the image using
 * threshold extraction, blur, and additive blending.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Glow effect with threshold, blur, and blend
 *
 * Extracts bright pixels above a threshold, blurs them, and blends
 * the result back with the original image. Creates a dreamy glow
 * effect around highlights.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | threshold | float | 0-1 | 0.8 | Brightness cutoff for bloom extraction |
 * | intensity | float | 0-5 | 1.0 | Bloom strength multiplier |
 * | radius | float | 1-50 | 10.0 | Blur radius in pixels |
 * | passes | int | 1-8 | 2 | Blur iterations for smoother glow |
 *
 * @par Example
 * @code
 * chain.add<Bloom>("glow")
 *     .input("source")
 *     .threshold(0.7f)
 *     .intensity(1.5f)
 *     .radius(15.0f);
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Texture with bloom effect applied
 */
class Bloom : public TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> threshold{"threshold", 0.8f, 0.0f, 1.0f};  ///< Brightness cutoff for bloom
    Param<float> intensity{"intensity", 1.0f, 0.0f, 5.0f};  ///< Bloom strength multiplier
    Param<float> radius{"radius", 10.0f, 1.0f, 50.0f};      ///< Blur radius in pixels
    Param<int> passes{"passes", 2, 1, 8};                    ///< Blur iterations

    /// @}
    // -------------------------------------------------------------------------

    Bloom() {
        registerParam(threshold);
        registerParam(intensity);
        registerParam(radius);
        registerParam(passes);
    }
    ~Bloom() override;

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Bloom"; }

    /// @}

private:
    void createPipeline(Context& ctx);
    void updateBindGroups(Context& ctx, WGPUTextureView inView);

    // GPU resources - need multiple passes
    WGPURenderPipeline m_thresholdPipeline = nullptr;
    WGPURenderPipeline m_blurHPipeline = nullptr;
    WGPURenderPipeline m_blurVPipeline = nullptr;
    WGPURenderPipeline m_combinePipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBindGroupLayout m_combineLayout = nullptr;  // Cached combine layout
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    // Intermediate textures
    WGPUTexture m_brightTexture = nullptr;
    WGPUTextureView m_brightView = nullptr;
    WGPUTexture m_blurTexture = nullptr;
    WGPUTextureView m_blurView = nullptr;

    // Cached bind groups (avoid per-frame recreation)
    WGPUBindGroup m_thresholdBindGroup = nullptr;
    WGPUBindGroup m_blurHBindGroup = nullptr;
    WGPUBindGroup m_blurVBindGroup = nullptr;
    WGPUBindGroup m_combineBindGroup = nullptr;
    WGPUTextureView m_lastInputView = nullptr;  // Track input changes
};

} // namespace vivid::effects
