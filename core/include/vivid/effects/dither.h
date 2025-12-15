#pragma once

/**
 * @file dither.h
 * @brief Ordered dithering operator
 *
 * Applies Bayer pattern dithering for retro-style color reduction.
 */

#include <vivid/effects/simple_texture_effect.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Dithering pattern types
 */
enum class DitherPattern {
    Bayer2x2,   ///< 2x2 Bayer matrix - coarse dithering
    Bayer4x4,   ///< 4x4 Bayer matrix - medium dithering
    Bayer8x8    ///< 8x8 Bayer matrix - fine dithering
};

/// @brief Uniform buffer for Dither effect
struct DitherUniforms {
    int pattern;
    int levels;
    float strength;
    float _pad;
};

/**
 * @brief Ordered dithering effect
 *
 * Reduces color depth using ordered (Bayer) dithering patterns.
 * Creates a retro aesthetic reminiscent of early computer graphics.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | levels | int | 2-256 | 8 | Color levels per channel |
 * | strength | float | 0-1 | 1.0 | Blend with original |
 *
 * @par Example
 * @code
 * auto& dither = chain.add<Dither>("dither");
 * dither.input(&source);
 * dither.pattern(DitherPattern::Bayer4x4);
 * dither.levels = 4;
 * dither.strength = 1.0f;
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Dithered texture
 */
class Dither : public SimpleTextureEffect<Dither, DitherUniforms> {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<int> levels{"levels", 8, 2, 256};         ///< Color levels per channel
    Param<float> strength{"strength", 1.0f, 0.0f, 1.0f}; ///< Effect strength

    /// @}
    // -------------------------------------------------------------------------

    Dither() {
        registerParam(levels);
        registerParam(strength);
    }

    /// @brief Set input texture
    void input(TextureOperator* op) { setInput(0, op); }

    /// @brief Set dither pattern (enum, not a Param)
    void pattern(DitherPattern p) {
        if (m_pattern != p) { m_pattern = p; markDirty(); }
    }

    /// @brief Get uniform values for GPU
    DitherUniforms getUniforms() const {
        return {
            static_cast<int>(m_pattern),
            levels,
            strength,
            0.0f
        };
    }

    /// @brief Use nearest-neighbor sampler to avoid blending artifacts
    WGPUSampler getSampler(WGPUDevice device) override {
        return gpu::getNearestClampSampler(device);
    }

    std::string name() const override { return "Dither"; }

    /// @brief Fragment shader source (used by CRTP base)
    const char* fragmentShader() const override;

private:
    DitherPattern m_pattern = DitherPattern::Bayer4x4;
};

} // namespace vivid::effects
