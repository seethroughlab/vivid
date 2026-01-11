#pragma once

/**
 * @file tone_map.h
 * @brief HDR to SDR tone mapping operator
 *
 * Maps high dynamic range values to displayable range.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>
#include <vivid/operator_registry.h>

namespace vivid::effects {

/**
 * @brief Tone mapping modes
 */
enum class ToneMapMode {
    Reinhard = 0,  ///< Simple Reinhard (x / (1 + x))
    ACES = 1,      ///< ACES filmic tone mapping
    Filmic = 2     ///< Uncharted 2 filmic curve
};

/**
 * @brief HDR to SDR tone mapping effect
 *
 * Converts high dynamic range color values to a displayable 0-1 range
 * using various industry-standard tone mapping curves.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | exposure | float | 0.1-10 | 1.0 | Pre-mapping exposure multiplier |
 * | mode | int | 0-2 | 0 | Tone map curve (Reinhard/ACES/Filmic) |
 * | whitePoint | float | 1-20 | 4.0 | White point for extended Reinhard |
 *
 * @par Example
 * @code
 * auto& tonemap = chain.add<ToneMap>("tonemap");
 * tonemap.input(&hdrSource);
 * tonemap.exposure = 1.5f;   // Boost exposure
 * tonemap.mode = 1;          // ACES curve
 * @endcode
 *
 * @par Inputs
 * - Input 0: HDR source texture
 *
 * @par Output
 * LDR (0-1 range) texture suitable for display
 *
 * @par TouchDesigner Equivalent
 * Tone Map TOP
 */
class ToneMap : public TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> exposure{"exposure", 1.0f, 0.1f, 10.0f};    ///< Exposure multiplier
    Param<int> mode{"mode", 0, 0, 2};                         ///< Tone map mode
    Param<float> whitePoint{"whitePoint", 4.0f, 1.0f, 20.0f}; ///< White point (Reinhard)

    /// @}
    // -------------------------------------------------------------------------

    ToneMap() {
        registerParam(exposure);
        registerParam(mode);
        registerParam(whitePoint);
    }
    ~ToneMap() override;

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "ToneMap"; }

    /// @}

private:
    void createPipeline(Context& ctx);

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;
};

} // namespace vivid::effects
