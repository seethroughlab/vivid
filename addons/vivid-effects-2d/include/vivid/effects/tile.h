#pragma once

/**
 * @file tile.h
 * @brief Texture tiling/repetition operator
 *
 * Tiles and repeats textures with offset and mirroring options.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

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
class Tile : public TextureOperator {
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
    ~Tile() override;

    /// @brief Set input texture
    Tile& input(TextureOperator* op) { setInput(0, op); return *this; }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Tile"; }

    /// @}

private:
    void createPipeline(Context& ctx);

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
