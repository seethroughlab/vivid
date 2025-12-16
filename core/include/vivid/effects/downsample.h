#pragma once

/**
 * @file downsample.h
 * @brief Resolution reduction operator
 *
 * Renders at a lower resolution and upscales for retro or performance effects.
 */

#include <vivid/effects/simple_texture_effect.h>
#include <vivid/param.h>

namespace vivid::effects {

/// @brief Uniform buffer for Downsample effect
struct DownsampleUniforms {
    float targetW;
    float targetH;
    float sourceW;
    float sourceH;
};

/**
 * @brief Upscale filter modes
 */
enum class FilterMode {
    Nearest,   ///< Point sampling - pixelated look
    Linear     ///< Bilinear interpolation - smooth scaling
};

/**
 * @brief Low-resolution rendering with upscale
 *
 * Renders the input at a lower resolution and upscales to output size.
 * Useful for retro pixel art aesthetics or performance optimization.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | targetW | int | 16-1920 | 320 | Target width in pixels |
 * | targetH | int | 16-1080 | 240 | Target height in pixels |
 *
 * @par Example
 * @code
 * chain.add<Downsample>("lowres")
 *     .input("source")
 *     .resolution(160, 120)
 *     .filter(FilterMode::Nearest);
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Downsampled and upscaled texture
 */
class Downsample : public SimpleTextureEffect<Downsample, DownsampleUniforms> {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<int> targetW{"targetW", 320, 16, 1920}; ///< Target width in pixels
    Param<int> targetH{"targetH", 240, 16, 1080}; ///< Target height in pixels

    /// @}
    // -------------------------------------------------------------------------

    Downsample() {
        registerParam(targetW);
        registerParam(targetH);
    }

    /// @brief Set input texture
    void input(TextureOperator* op) { setInput(0, op); }

    /// @brief Set upscale filter mode (Nearest = pixelated, Linear = smooth)
    void filter(FilterMode f) { if (m_filter != f) { m_filter = f; markDirty(); } }

    /// @brief Get uniform values for GPU
    DownsampleUniforms getUniforms() const {
        return {
            static_cast<float>(static_cast<int>(targetW)),
            static_cast<float>(static_cast<int>(targetH)),
            static_cast<float>(m_width),
            static_cast<float>(m_height)
        };
    }

    std::string name() const override { return "Downsample"; }

    /// @brief Fragment shader source (used by CRTP base)
    const char* fragmentShader() const override;

private:
    FilterMode m_filter = FilterMode::Nearest;
};

#ifdef _WIN32
extern template class SimpleTextureEffect<Downsample, DownsampleUniforms>;
#endif

} // namespace vivid::effects
