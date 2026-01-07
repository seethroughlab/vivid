#pragma once

/**
 * @file hsv.h
 * @brief HSV color adjustment operator
 *
 * Adjust hue, saturation, and value (brightness) in HSV color space.
 */

#include <vivid/effects/simple_texture_effect.h>
#include <vivid/param.h>

namespace vivid::effects {

/// @brief Uniform buffer for HSV effect
struct HSVUniforms {
    float hueShift;
    float saturation;
    float value;
    float _pad;
};

/**
 * @brief HSV color adjustment
 *
 * Converts to HSV color space, applies adjustments, and converts back to RGB.
 * Useful for color grading, hue rotation, and desaturation effects.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | hueShift | float | 0-1 | 0.0 | Hue rotation (0-1 = full 360° rotation) |
 * | saturation | float | 0-3 | 1.0 | Saturation multiplier (0 = grayscale) |
 * | value | float | 0-3 | 1.0 | Value/brightness multiplier |
 *
 * @par Example
 * @code
 * auto& hsv = chain.add<HSV>("hsv");
 * hsv.input(&source);
 * hsv.hueShift = 0.5f;      // Shift hue 180°
 * hsv.saturation = 1.2f;    // Boost saturation
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Color-adjusted texture
 */
class HSV : public SimpleTextureEffect<HSV, HSVUniforms> {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> hueShift{"hueShift", 0.0f, 0.0f, 1.0f};     ///< Hue rotation (0-1 wraps)
    Param<float> saturation{"saturation", 1.0f, 0.0f, 3.0f}; ///< Saturation multiplier
    Param<float> value{"value", 1.0f, 0.0f, 3.0f};           ///< Value/brightness multiplier

    /// @}
    // -------------------------------------------------------------------------

    HSV() {
        registerParam(hueShift);
        registerParam(saturation);
        registerParam(value);
    }

    /// @brief Get uniform values for GPU
    HSVUniforms getUniforms() const {
        return {hueShift, saturation, value, 0.0f};
    }

    std::string name() const override { return "HSV"; }

    /// @brief Fragment shader source (used by CRTP base)
    const char* fragmentShader() const override;
};

// Extern template declaration - suppresses instantiation in user code on Windows
// The actual instantiation is in hsv.cpp (vivid-core.dll)
#ifdef _WIN32
extern template class SimpleTextureEffect<HSV, HSVUniforms>;
#endif

} // namespace vivid::effects
