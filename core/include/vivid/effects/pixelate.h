#pragma once

/**
 * @file pixelate.h
 * @brief Mosaic/pixelation operator
 *
 * Creates a pixelated mosaic effect by sampling at lower resolution.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

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
class Pixelate : public TextureOperator {
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
    ~Pixelate() override;

    /// @brief Set input texture
    void input(TextureOperator* op) { setInput(0, op); }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Pixelate"; }

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
