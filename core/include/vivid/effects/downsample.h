#pragma once

/**
 * @file downsample.h
 * @brief Resolution reduction operator
 *
 * Renders at a lower resolution and upscales for retro or performance effects.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

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
class Downsample : public TextureOperator {
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
    ~Downsample() override;

    /// @brief Set input texture
    void input(TextureOperator* op) { setInput(0, op); }

    /// @brief Set upscale filter mode (Nearest = pixelated, Linear = smooth)
    void filter(FilterMode f) { if (m_filter != f) { m_filter = f; markDirty(); } }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Downsample"; }

    /// @}

private:
    void createPipeline(Context& ctx);

    FilterMode m_filter = FilterMode::Nearest;

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
