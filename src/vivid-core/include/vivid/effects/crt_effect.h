#pragma once

/**
 * @file crt_effect.h
 * @brief Retro CRT monitor simulation
 *
 * Combines multiple effects to simulate a vintage CRT display.
 */

#include <vivid/effects/simple_texture_effect.h>
#include <vivid/param.h>

namespace vivid::effects {

/// @brief Uniform buffer for CRT effect
struct CRTEffectUniforms {
    float curvature;
    float vignette;
    float scanlines;
    float bloom;
    float chromatic;
    float aspect;
    float _pad[2];
};

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
class CRTEffect : public SimpleTextureEffect<CRTEffect, CRTEffectUniforms> {
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

    /// @brief Get uniform values for GPU
    CRTEffectUniforms getUniforms() const {
        return {curvature, vignette, scanlines, bloom, chromatic, static_cast<float>(m_width) / m_height, {0, 0}};
    }

    std::string name() const override { return "CRTEffect"; }

    /// @brief Fragment shader source (used by CRTP base)
    const char* fragmentShader() const override;
};

#ifdef _WIN32
extern template class SimpleTextureEffect<CRTEffect, CRTEffectUniforms>;
#endif

} // namespace vivid::effects
