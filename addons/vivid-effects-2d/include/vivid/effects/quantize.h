#pragma once

/**
 * @file quantize.h
 * @brief Color quantization operator
 *
 * Reduces color palette by quantizing to discrete levels.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

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
 * chain.add<Quantize>("posterize")
 *     .input("source")
 *     .levels(4);  // 4 levels = 64 total colors (4^3)
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Quantized texture with reduced color palette
 */
class Quantize : public TextureOperator {
public:
    Quantize() = default;
    ~Quantize() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set input texture
     * @param op Source operator
     * @return Reference for chaining
     */
    Quantize& input(TextureOperator* op) { setInput(0, op); return *this; }

    /**
     * @brief Set color levels per channel
     * @param n Levels (2-256, default 8)
     * @return Reference for chaining
     */
    Quantize& levels(int n) { m_levels = n; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Quantize"; }

    std::vector<ParamDecl> params() override {
        return { m_levels.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "levels") { out[0] = m_levels; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "levels") { m_levels = static_cast<int>(value[0]); return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    Param<int> m_levels{"levels", 8, 2, 256};

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
