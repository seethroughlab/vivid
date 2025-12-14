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
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> curvature{"curvature", 0.1f, 0.0f, 0.5f};    ///< Barrel distortion amount
    Param<float> vignette{"vignette", 0.3f, 0.0f, 1.0f};      ///< Edge darkening intensity
    Param<float> scanlines{"scanlines", 0.2f, 0.0f, 1.0f};    ///< Scanline visibility
    Param<float> bloom{"bloom", 0.1f, 0.0f, 1.0f};            ///< Phosphor glow intensity
    Param<float> chromatic{"chromatic", 0.02f, 0.0f, 0.1f};   ///< RGB separation amount

    /// @}
    // -------------------------------------------------------------------------

    CRTEffect() {
        registerParam(curvature);
        registerParam(vignette);
        registerParam(scanlines);
        registerParam(bloom);
        registerParam(chromatic);
    }
    ~CRTEffect() override;

    /// @brief Set input texture
    void input(TextureOperator* op) { setInput(0, op); }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "CRTEffect"; }

    /// @}

private:
    void createPipeline(Context& ctx);

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
