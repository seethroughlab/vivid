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
    Bloom() = default;
    ~Bloom() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set input texture
     * @param op Source operator
     * @return Reference for chaining
     */
    Bloom& input(TextureOperator* op) { setInput(0, op); return *this; }

    /**
     * @brief Set brightness threshold
     * @param t Threshold (0-1, default 0.8). Pixels above this contribute to bloom
     * @return Reference for chaining
     */
    Bloom& threshold(float t) { if (m_threshold != t) { m_threshold = t; markDirty(); } return *this; }

    /**
     * @brief Set bloom intensity
     * @param i Intensity multiplier (0-5, default 1.0)
     * @return Reference for chaining
     */
    Bloom& intensity(float i) { if (m_intensity != i) { m_intensity = i; markDirty(); } return *this; }

    /**
     * @brief Set blur radius
     * @param r Radius in pixels (1-50, default 10.0)
     * @return Reference for chaining
     */
    Bloom& radius(float r) { if (m_radius != r) { m_radius = r; markDirty(); } return *this; }

    /**
     * @brief Set number of blur passes
     * @param p Pass count (1-8, default 2)
     * @return Reference for chaining
     */
    Bloom& passes(int p) { if (m_passes != p) { m_passes = p; markDirty(); } return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Bloom"; }

    std::vector<ParamDecl> params() override {
        return { m_threshold.decl(), m_intensity.decl(), m_radius.decl(), m_passes.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "threshold") { out[0] = m_threshold; return true; }
        if (name == "intensity") { out[0] = m_intensity; return true; }
        if (name == "radius") { out[0] = m_radius; return true; }
        if (name == "passes") { out[0] = m_passes; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "threshold") { threshold(value[0]); return true; }
        if (name == "intensity") { intensity(value[0]); return true; }
        if (name == "radius") { radius(value[0]); return true; }
        if (name == "passes") { passes(static_cast<int>(value[0])); return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    Param<float> m_threshold{"threshold", 0.8f, 0.0f, 1.0f};
    Param<float> m_intensity{"intensity", 1.0f, 0.0f, 5.0f};
    Param<float> m_radius{"radius", 10.0f, 1.0f, 50.0f};
    Param<int> m_passes{"passes", 2, 1, 8};

    // GPU resources - need multiple passes
    WGPURenderPipeline m_thresholdPipeline = nullptr;
    WGPURenderPipeline m_blurHPipeline = nullptr;
    WGPURenderPipeline m_blurVPipeline = nullptr;
    WGPURenderPipeline m_combinePipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    // Intermediate textures
    WGPUTexture m_brightTexture = nullptr;
    WGPUTextureView m_brightView = nullptr;
    WGPUTexture m_blurTexture = nullptr;
    WGPUTextureView m_blurView = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
