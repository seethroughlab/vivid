#pragma once

/**
 * @file dither.h
 * @brief Ordered dithering operator
 *
 * Applies Bayer pattern dithering for retro-style color reduction.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Dithering pattern types
 */
enum class DitherPattern {
    Bayer2x2,   ///< 2x2 Bayer matrix - coarse dithering
    Bayer4x4,   ///< 4x4 Bayer matrix - medium dithering
    Bayer8x8    ///< 8x8 Bayer matrix - fine dithering
};

/**
 * @brief Ordered dithering effect
 *
 * Reduces color depth using ordered (Bayer) dithering patterns.
 * Creates a retro aesthetic reminiscent of early computer graphics.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | levels | int | 2-256 | 8 | Color levels per channel |
 * | strength | float | 0-1 | 1.0 | Blend with original |
 *
 * @par Example
 * @code
 * auto& dither = chain.add<Dither>("dither");
 * dither.input(&source);
 * dither.pattern(DitherPattern::Bayer4x4);
 * dither.levels = 4;
 * dither.strength = 1.0f;
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Dithered texture
 */
class Dither : public TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<int> levels{"levels", 8, 2, 256};         ///< Color levels per channel
    Param<float> strength{"strength", 1.0f, 0.0f, 1.0f}; ///< Effect strength

    /// @}
    // -------------------------------------------------------------------------

    Dither() {
        registerParam(levels);
        registerParam(strength);
    }
    ~Dither() override;

    /// @brief Set input texture
    Dither& input(TextureOperator* op) { setInput(0, op); return *this; }

    /// @brief Set dither pattern (enum, not a Param)
    Dither& pattern(DitherPattern p) {
        if (m_pattern != p) { m_pattern = p; markDirty(); }
        return *this;
    }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Dither"; }

    /// @}

private:
    void createPipeline(Context& ctx);

    DitherPattern m_pattern = DitherPattern::Bayer4x4;

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
