#pragma once

/**
 * @file tile.h
 * @brief Texture tiling/repetition operator
 *
 * Tiles and repeats textures with offset and mirroring options.
 */

#include <vivid/effects/simple_texture_effect.h>
#include <vivid/param.h>

namespace vivid::effects {

/// @brief Uniform buffer for Tile effect
struct TileUniforms {
    float repeatX;
    float repeatY;
    float offsetX;
    float offsetY;
    int mirror;
    float _pad[3];
};

/**
 * @brief Texture tiling/repetition effect
 *
 * Repeats the input texture across the output with configurable
 * repeat count, offset, and optional mirroring at tile boundaries.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | repeat | vec2 | 0.1-20 | (2,2) | Tile repetition count |
 * | offset | vec2 | -1 to 1 | (0,0) | UV offset |
 * | mirror | bool | - | false | Mirror at tile boundaries |
 *
 * @par Example
 * @code
 * chain.add<Tile>("tiled")
 *     .input("source")
 *     .repeat(4.0f)       // 4x4 tile grid
 *     .offset(0.25f, 0.0f)
 *     .mirror(true);
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Tiled texture
 */
class Tile : public SimpleTextureEffect<Tile, TileUniforms> {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Vec2Param repeat{"repeat", 2.0f, 2.0f, 0.1f, 20.0f};  ///< Tile repetition count
    Vec2Param offset{"offset", 0.0f, 0.0f, -1.0f, 1.0f};  ///< UV offset
    Param<bool> mirror{"mirror", false};                   ///< Mirror at boundaries

    /// @}
    // -------------------------------------------------------------------------

    Tile() {
        registerParam(repeat);
        registerParam(offset);
        registerParam(mirror);
    }

    /// @brief Get uniform values for GPU
    TileUniforms getUniforms() const {
        return {
            repeat.x(),
            repeat.y(),
            offset.x(),
            offset.y(),
            mirror ? 1 : 0,
            {0, 0, 0}
        };
    }

    /// @brief Use linear repeat sampler instead of clamp
    WGPUSampler getSampler(WGPUDevice device) override {
        return gpu::getLinearRepeatSampler(device);
    }

    std::string name() const override { return "Tile"; }

    /// @brief Fragment shader source (used by CRTP base)
    const char* fragmentShader() const override;
};

#ifdef _WIN32
extern template class SimpleTextureEffect<Tile, TileUniforms>;
#endif

} // namespace vivid::effects
