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
 * auto& posterize = chain.add<Quantize>("posterize");
 * posterize.input(&source);
 * posterize.levels = 4;  // 4 levels = 64 total colors (4^3)
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
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<int> levels{"levels", 8, 2, 256}; ///< Color levels per channel

    /// @}
    // -------------------------------------------------------------------------

    Quantize() {
        registerParam(levels);
    }
    ~Quantize() override;

    /// @brief Set input texture
    void input(TextureOperator* op) { setInput(0, op); }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Quantize"; }

    /// @}

private:
    void createPipeline(Context& ctx);

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
