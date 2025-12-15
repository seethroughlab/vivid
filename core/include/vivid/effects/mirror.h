#pragma once

/**
 * @file mirror.h
 * @brief Mirror and kaleidoscope operator
 *
 * Applies axis mirroring and radial kaleidoscope effects.
 */

#include <vivid/effects/simple_texture_effect.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Mirror mode types
 */
enum class MirrorMode {
    Horizontal,     ///< Left-right mirror
    Vertical,       ///< Top-bottom mirror
    Quad,           ///< Both axes (4 quadrants)
    Kaleidoscope    ///< Radial symmetry with segments
};

/// @brief Uniform buffer for Mirror effect
struct MirrorUniforms {
    int mode;
    int segments;
    float angle;
    float centerX;
    float centerY;
    float _pad[3];
};

/**
 * @brief Mirror and kaleidoscope effects
 *
 * Applies various mirroring effects including simple axis mirroring
 * and kaleidoscope-style radial symmetry.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | segments | int | 2-32 | 6 | Kaleidoscope segment count |
 * | angle | float | -2π to 2π | 0.0 | Rotation angle (kaleidoscope) |
 * | center | vec2 | 0-1 | (0.5,0.5) | Mirror center point |
 *
 * @par Example
 * @code
 * auto& kaleido = chain.add<Mirror>("kaleido");
 * kaleido.input(&source);
 * kaleido.mode(MirrorMode::Kaleidoscope);
 * kaleido.segments = 8;
 * kaleido.angle = time * 0.1f;
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Mirrored texture
 */
class Mirror : public SimpleTextureEffect<Mirror, MirrorUniforms> {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<int> segments{"segments", 6, 2, 32};       ///< Kaleidoscope segments
    Param<float> angle{"angle", 0.0f, -6.28f, 6.28f}; ///< Rotation angle
    Vec2Param center{"center", 0.5f, 0.5f, 0.0f, 1.0f}; ///< Center point

    /// @}
    // -------------------------------------------------------------------------

    Mirror() {
        registerParam(segments);
        registerParam(angle);
        registerParam(center);
    }

    /// @brief Set input texture
    void input(TextureOperator* op) { setInput(0, op); }

    /// @brief Set mirror mode (enum, not a Param)
    void mode(MirrorMode m) {
        if (m_mode != m) { m_mode = m; markDirty(); }
    }

    /// @brief Get uniform values for GPU
    MirrorUniforms getUniforms() const {
        return {
            static_cast<int>(m_mode),
            segments,
            angle,
            center.x(),
            center.y(),
            {0, 0, 0}
        };
    }

    std::string name() const override { return "Mirror"; }

    /// @brief Fragment shader source (used by CRTP base)
    const char* fragmentShader() const override;

private:
    MirrorMode m_mode = MirrorMode::Horizontal;
};

} // namespace vivid::effects
