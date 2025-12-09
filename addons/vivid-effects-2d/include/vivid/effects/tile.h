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
    Tile() = default;
    ~Tile() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set input texture
     * @param op Source operator
     * @return Reference for chaining
     */
    Tile& input(TextureOperator* op) { setInput(0, op); return *this; }

    /**
     * @brief Set uniform repeat count
     * @param r Repeat count (applies to both axes)
     * @return Reference for chaining
     */
    Tile& repeat(float r) { m_repeat.set(r, r); return *this; }

    /**
     * @brief Set non-uniform repeat count
     * @param x X repeat count
     * @param y Y repeat count
     * @return Reference for chaining
     */
    Tile& repeat(float x, float y) { m_repeat.set(x, y); return *this; }

    /**
     * @brief Set UV offset
     * @param x X offset (-1 to 1)
     * @param y Y offset (-1 to 1)
     * @return Reference for chaining
     */
    Tile& offset(float x, float y) { m_offset.set(x, y); return *this; }

    /**
     * @brief Enable tile boundary mirroring
     * @param m True to mirror at boundaries
     * @return Reference for chaining
     */
    Tile& mirror(bool m) { m_mirror = m; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Tile"; }

    std::vector<ParamDecl> params() override {
        return { m_repeat.decl(), m_offset.decl(), m_mirror.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "repeat") { out[0] = m_repeat.x(); out[1] = m_repeat.y(); return true; }
        if (name == "offset") { out[0] = m_offset.x(); out[1] = m_offset.y(); return true; }
        if (name == "mirror") { out[0] = m_mirror ? 1.0f : 0.0f; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "repeat") { m_repeat.set(value[0], value[1]); return true; }
        if (name == "offset") { m_offset.set(value[0], value[1]); return true; }
        if (name == "mirror") { m_mirror = value[0] > 0.5f; return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    Vec2Param m_repeat{"repeat", 2.0f, 2.0f, 0.1f, 20.0f};
    Vec2Param m_offset{"offset", 0.0f, 0.0f, -1.0f, 1.0f};
    Param<bool> m_mirror{"mirror", false};

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
