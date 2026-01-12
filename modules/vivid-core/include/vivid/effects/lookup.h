#pragma once

/**
 * @file lookup.h
 * @brief Color lookup table operator
 *
 * Remap colors using a 1D gradient or LUT texture.
 * Perfect for colorizing grayscale or applying color grading.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>
#include <vivid/operator_registry.h>

namespace vivid::effects {

/**
 * @brief Lookup mode
 */
enum class LookupMode {
    Luminance,  ///< Use input luminance to sample LUT horizontally
    Red,        ///< Use red channel to sample LUT
    Green,      ///< Use green channel to sample LUT
    Blue        ///< Use blue channel to sample LUT
};

/**
 * @brief Color lookup table effect
 *
 * Remaps input colors by sampling from a lookup texture (LUT).
 * The most common use is colorizing grayscale: input luminance
 * becomes the U coordinate to sample a gradient texture.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | intensity | float | 0-1 | 1.0 | Blend between original and LUT result |
 * | mode | enum | - | Luminance | Channel to use for lookup (Luminance/R/G/B) |
 *
 * @par Example: Colorize grayscale noise
 * @code
 * // Create grayscale noise
 * auto& noise = chain.add<Noise>("noise");
 *
 * // Create a color gradient as the LUT
 * auto& gradient = chain.add<Gradient>("gradient");
 * gradient.colorA.set(0.0f, 0.0f, 0.5f, 1.0f);  // Dark blue (for dark values)
 * gradient.colorB.set(1.0f, 0.5f, 0.0f, 1.0f);  // Orange (for bright values)
 *
 * // Colorize: grayscale -> sample gradient -> color output
 * auto& colorize = chain.add<Lookup>("colorize");
 * colorize.input("noise");        // Source texture
 * colorize.lut("gradient");       // LUT/gradient texture
 * @endcode
 *
 * @par Example: Heat map visualization
 * @code
 * // Use Ramp for a multi-color heat map LUT
 * auto& heatmap = chain.add<Ramp>("heatmap");
 * heatmap.hueSpeed = 0.0f;  // Static
 *
 * auto& lookup = chain.add<Lookup>("lookup");
 * lookup.input("data");
 * lookup.lut("heatmap");
 * lookup.mode = LookupMode::Red;  // Use red channel as lookup key
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture (grayscale or RGB)
 * - Input 1 (lut): Lookup texture (typically a horizontal gradient)
 *
 * @par Output
 * Color-remapped texture
 *
 * @par TouchDesigner Equivalent
 * Lookup TOP
 *
 * @see projects/getting-started/02-operator-pipeline (colorize noise example)
 * @see Gradient, Ramp, HSV, Level
 */
class Lookup : public TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> intensity{"intensity", 1.0f, 0.0f, 1.0f};  ///< Blend intensity
    EnumParam<LookupMode> mode{"mode", LookupMode::Luminance};  ///< Which channel to use

    /// @}
    // -------------------------------------------------------------------------

    Lookup() {
        registerParam(intensity);
        registerParam(mode);
    }
    ~Lookup() override;

    /// @brief Set the LUT/gradient texture input
    void lut(const std::string& name) {
        setInputByName(1, name);
    }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Lookup"; }

    /// @}

    /**
     * @brief Get lookup mode display name
     * @param m Lookup mode
     * @return Human-readable name
     */
    static const char* modeName(LookupMode m) {
        switch (m) {
            case LookupMode::Luminance: return "Luminance";
            case LookupMode::Red: return "Red";
            case LookupMode::Green: return "Green";
            case LookupMode::Blue: return "Blue";
            default: return "Unknown";
        }
    }

private:
    void createPipeline(Context& ctx);
    void updateBindGroup(Context& ctx);

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    // Track input changes
    WGPUTextureView m_lastSourceView = nullptr;
    WGPUTextureView m_lastLutView = nullptr;
};

} // namespace vivid::effects
