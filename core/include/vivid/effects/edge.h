#pragma once

/**
 * @file edge.h
 * @brief Edge detection operator
 *
 * Detects edges using the Sobel operator.
 */

#include <vivid/effects/simple_texture_effect.h>
#include <vivid/param.h>

namespace vivid::effects {

/// @brief Uniform buffer for Edge effect
struct EdgeUniforms {
    float strength;
    float threshold;
    float texelW;
    float texelH;
    int invert;
    float _pad[3];
};

/**
 * @brief Sobel edge detection
 *
 * Applies Sobel edge detection to highlight edges in the image.
 * Outputs edge intensity as grayscale.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | strength | float | 0-5 | 1.0 | Edge intensity multiplier |
 * | threshold | float | 0-1 | 0.0 | Minimum edge value to show |
 * | invert | bool | - | false | Invert output (white background) |
 *
 * @par Example
 * @code
 * auto& edges = chain.add<Edge>("edges");
 * edges.input(&source);
 * edges.strength = 2.0f;
 * edges.threshold = 0.1f;
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Grayscale edge map
 */
class Edge : public SimpleTextureEffect<Edge, EdgeUniforms> {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> strength{"strength", 1.0f, 0.0f, 5.0f};   ///< Edge intensity
    Param<float> threshold{"threshold", 0.0f, 0.0f, 1.0f}; ///< Minimum edge value
    Param<bool> invert{"invert", false};                    ///< Invert output

    /// @}
    // -------------------------------------------------------------------------

    Edge() {
        registerParam(strength);
        registerParam(threshold);
        registerParam(invert);
    }

    /// @brief Get uniform values for GPU
    EdgeUniforms getUniforms() const {
        return {
            strength,
            threshold,
            1.0f / m_width,
            1.0f / m_height,
            invert ? 1 : 0,
            {0, 0, 0}
        };
    }

    std::string name() const override { return "Edge"; }

    /// @brief Fragment shader source (used by CRTP base)
    const char* fragmentShader() const override;
};

#ifdef _WIN32
extern template class SimpleTextureEffect<Edge, EdgeUniforms>;
#endif

} // namespace vivid::effects
