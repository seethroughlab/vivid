#pragma once

/**
 * @file pixelate.h
 * @brief Mosaic/pixelation operator
 *
 * Creates a pixelated mosaic effect by sampling at lower resolution.
 */

#include <vivid/effects/simple_texture_effect.h>
#include <vivid/param.h>

namespace vivid::effects {

/// @brief Uniform buffer for Pixelate effect
struct PixelateUniforms {
    float sizeX;
    float sizeY;
    float texWidth;
    float texHeight;
};

/**
 * @brief Mosaic/pixelation effect
 *
 * Reduces effective resolution by sampling pixels in blocks,
 * creating a mosaic or retro pixel art appearance.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | size | vec2 | 1-100 | (10,10) | Pixel block size in screen pixels |
 *
 * @par Example
 * @code
 * chain.add<Pixelate>("pixels")
 *     .input("source")
 *     .size(16.0f);  // 16x16 pixel blocks
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Pixelated texture
 */
class Pixelate : public SimpleTextureEffect<Pixelate, PixelateUniforms> {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Vec2Param size{"size", 10.0f, 10.0f, 1.0f, 100.0f}; ///< Pixel block size

    /// @}
    // -------------------------------------------------------------------------

    Pixelate() {
        registerParam(size);
    }

    /// @brief Get uniform values for GPU
    PixelateUniforms getUniforms() const {
        return {size.x(), size.y(), static_cast<float>(m_width), static_cast<float>(m_height)};
    }

    std::string name() const override { return "Pixelate"; }

    /// @brief Fragment shader source (used by CRTP base)
    const char* fragmentShader() const override;
};

#ifdef _WIN32
extern template class SimpleTextureEffect<Pixelate, PixelateUniforms>;
#endif

} // namespace vivid::effects
