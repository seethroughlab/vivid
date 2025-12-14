#pragma once

/**
 * @file scanlines.h
 * @brief CRT-style scanlines operator
 *
 * Adds horizontal or vertical scanlines for retro CRT aesthetics.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief CRT-style scanlines effect
 *
 * Overlays horizontal or vertical scanlines to simulate CRT display
 * artifacts. Commonly used for retro gaming aesthetics.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | spacing | int | 1-20 | 2 | Pixels between scanlines |
 * | thickness | float | 0-1 | 0.5 | Scanline thickness ratio |
 * | intensity | float | 0-1 | 0.3 | Darkening intensity |
 * | vertical | bool | - | false | Use vertical instead of horizontal |
 *
 * @par Example
 * @code
 * auto& crt = chain.add<Scanlines>("crt");
 * crt.input(&source);
 * crt.spacing = 2;
 * crt.intensity = 0.4f;
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Texture with scanline overlay
 */
class Scanlines : public TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<int> spacing{"spacing", 2, 1, 20};           ///< Pixels between scanlines
    Param<float> thickness{"thickness", 0.5f, 0.0f, 1.0f}; ///< Scanline thickness
    Param<float> intensity{"intensity", 0.3f, 0.0f, 1.0f}; ///< Darkening intensity
    Param<bool> vertical{"vertical", false};            ///< Use vertical lines

    /// @}
    // -------------------------------------------------------------------------

    Scanlines() {
        registerParam(spacing);
        registerParam(thickness);
        registerParam(intensity);
        registerParam(vertical);
    }
    ~Scanlines() override;

    /// @brief Set input texture
    Scanlines& input(TextureOperator* op) { setInput(0, op); return *this; }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Scanlines"; }

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
