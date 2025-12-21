#pragma once

/**
 * @file quantize.h
 * @brief Color quantization operator
 *
 * Reduces color palette by quantizing to discrete levels.
 */

#include <vivid/effects/simple_texture_effect.h>
#include <vivid/param.h>

namespace vivid::effects {

/// @brief Uniform buffer for Quantize effect
struct QuantizeUniforms {
    int levels;
    float _pad[3];
};

/**
 * @brief Color quantization effect
 *
 * Reduces the number of colors by quantizing each channel to
 * a specified number of discrete levels. Creates a posterized look.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | levels | int | 2-256 | 8 | Color levels per channel |
 *
 * @par Example
 * @code
 * auto& posterize = chain.add<Quantize>("posterize");
 * posterize.input(&source);
 * posterize.levels = 4;  // 4 levels = 64 total colors (4^3)
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Quantized texture with reduced color palette
 */
class Quantize : public SimpleTextureEffect<Quantize, QuantizeUniforms> {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<int> levels{"levels", 8, 2, 256}; ///< Color levels per channel

    /// @}
    // -------------------------------------------------------------------------

    Quantize() {
        registerParam(levels);
    }

    /// @brief Get uniform values for GPU
    QuantizeUniforms getUniforms() const {
        return {levels, {0, 0, 0}};
    }

    std::string name() const override { return "Quantize"; }

    /// @brief Fragment shader source (used by CRTP base)
    const char* fragmentShader() const override;
};

#ifdef _WIN32
extern template class SimpleTextureEffect<Quantize, QuantizeUniforms>;
#endif

} // namespace vivid::effects
