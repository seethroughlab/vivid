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
 * chain.add<Dither>("dither")
 *     .input("source")
 *     .pattern(DitherPattern::Bayer4x4)
 *     .levels(4)
 *     .strength(1.0f);
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
    Dither() = default;
    ~Dither() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set input texture
     * @param op Source operator
     * @return Reference for chaining
     */
    Dither& input(TextureOperator* op) { setInput(0, op); return *this; }

    /**
     * @brief Set dither pattern
     * @param p Dither pattern (Bayer2x2, Bayer4x4, Bayer8x8)
     * @return Reference for chaining
     */
    Dither& pattern(DitherPattern p) { m_pattern = p; return *this; }

    /**
     * @brief Set color levels per channel
     * @param n Levels (2-256, default 8)
     * @return Reference for chaining
     */
    Dither& levels(int n) { m_levels = n; return *this; }

    /**
     * @brief Set effect strength
     * @param s Strength (0-1, default 1.0). 0 = original, 1 = full dither
     * @return Reference for chaining
     */
    Dither& strength(float s) { m_strength = s; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Dither"; }

    std::vector<ParamDecl> params() override {
        return { m_levels.decl(), m_strength.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "levels") { out[0] = m_levels; return true; }
        if (name == "strength") { out[0] = m_strength; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "levels") { m_levels = static_cast<int>(value[0]); return true; }
        if (name == "strength") { m_strength = value[0]; return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    DitherPattern m_pattern = DitherPattern::Bayer4x4;
    Param<int> m_levels{"levels", 8, 2, 256};
    Param<float> m_strength{"strength", 1.0f, 0.0f, 1.0f};

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
