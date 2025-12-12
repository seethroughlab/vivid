#pragma once

/**
 * @file crt_effect.h
 * @brief Retro CRT monitor simulation
 *
 * Combines multiple effects to simulate a vintage CRT display.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Retro CRT monitor simulation
 *
 * Applies a combination of barrel distortion, vignetting, scanlines,
 * phosphor bloom, and chromatic aberration to simulate a vintage CRT display.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | curvature | float | 0-0.5 | 0.1 | Barrel distortion amount |
 * | vignette | float | 0-1 | 0.3 | Edge darkening intensity |
 * | scanlines | float | 0-1 | 0.2 | Scanline visibility |
 * | bloom | float | 0-1 | 0.1 | Phosphor glow intensity |
 * | chromatic | float | 0-0.1 | 0.02 | RGB separation amount |
 *
 * @par Example
 * @code
 * chain.add<CRTEffect>("crt")
 *     .input("source")
 *     .curvature(0.15f)
 *     .scanlines(0.3f)
 *     .vignette(0.4f);
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * CRT-styled texture
 */
class CRTEffect : public TextureOperator {
public:
    CRTEffect() = default;
    ~CRTEffect() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set input texture
     * @param op Source operator
     * @return Reference for chaining
     */
    CRTEffect& input(TextureOperator* op) { setInput(0, op); return *this; }

    /**
     * @brief Set barrel distortion curvature
     * @param c Curvature amount (0-0.5, default 0.1)
     * @return Reference for chaining
     */
    CRTEffect& curvature(float c) {
        if (m_curvature != c) { m_curvature = c; markDirty(); }
        return *this;
    }

    /**
     * @brief Set vignette intensity
     * @param v Vignette amount (0-1, default 0.3)
     * @return Reference for chaining
     */
    CRTEffect& vignette(float v) {
        if (m_vignette != v) { m_vignette = v; markDirty(); }
        return *this;
    }

    /**
     * @brief Set scanline intensity
     * @param s Scanline amount (0-1, default 0.2)
     * @return Reference for chaining
     */
    CRTEffect& scanlines(float s) {
        if (m_scanlines != s) { m_scanlines = s; markDirty(); }
        return *this;
    }

    /**
     * @brief Set phosphor bloom intensity
     * @param b Bloom amount (0-1, default 0.1)
     * @return Reference for chaining
     */
    CRTEffect& bloom(float b) {
        if (m_bloom != b) { m_bloom = b; markDirty(); }
        return *this;
    }

    /**
     * @brief Set chromatic aberration amount
     * @param c Chromatic separation (0-0.1, default 0.02)
     * @return Reference for chaining
     */
    CRTEffect& chromatic(float c) {
        if (m_chromatic != c) { m_chromatic = c; markDirty(); }
        return *this;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "CRTEffect"; }

    std::vector<ParamDecl> params() override {
        return { m_curvature.decl(), m_vignette.decl(), m_scanlines.decl(),
                 m_bloom.decl(), m_chromatic.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "curvature") { out[0] = m_curvature; return true; }
        if (name == "vignette") { out[0] = m_vignette; return true; }
        if (name == "scanlines") { out[0] = m_scanlines; return true; }
        if (name == "bloom") { out[0] = m_bloom; return true; }
        if (name == "chromatic") { out[0] = m_chromatic; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "curvature") { curvature(value[0]); return true; }
        if (name == "vignette") { vignette(value[0]); return true; }
        if (name == "scanlines") { scanlines(value[0]); return true; }
        if (name == "bloom") { bloom(value[0]); return true; }
        if (name == "chromatic") { chromatic(value[0]); return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    Param<float> m_curvature{"curvature", 0.1f, 0.0f, 0.5f};
    Param<float> m_vignette{"vignette", 0.3f, 0.0f, 1.0f};
    Param<float> m_scanlines{"scanlines", 0.2f, 0.0f, 1.0f};
    Param<float> m_bloom{"bloom", 0.1f, 0.0f, 1.0f};
    Param<float> m_chromatic{"chromatic", 0.02f, 0.0f, 0.1f};

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
