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
 * chain.add<Scanlines>("crt")
 *     .input("source")
 *     .spacing(2)
 *     .intensity(0.4f);
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
    Scanlines() = default;
    ~Scanlines() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set input texture
     * @param op Source operator
     * @return Reference for chaining
     */
    Scanlines& input(TextureOperator* op) { setInput(0, op); return *this; }

    /**
     * @brief Set scanline spacing
     * @param pixels Pixels between scanlines (1-20, default 2)
     * @return Reference for chaining
     */
    Scanlines& spacing(int pixels) { m_spacing = pixels; return *this; }

    /**
     * @brief Set scanline thickness
     * @param t Thickness ratio (0-1, default 0.5)
     * @return Reference for chaining
     */
    Scanlines& thickness(float t) { m_thickness = t; return *this; }

    /**
     * @brief Set darkening intensity
     * @param i Intensity (0-1, default 0.3)
     * @return Reference for chaining
     */
    Scanlines& intensity(float i) { m_intensity = i; return *this; }

    /**
     * @brief Use vertical scanlines
     * @param v True for vertical, false for horizontal
     * @return Reference for chaining
     */
    Scanlines& vertical(bool v) { m_vertical = v; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Scanlines"; }

    std::vector<ParamDecl> params() override {
        return { m_spacing.decl(), m_thickness.decl(), m_intensity.decl(), m_vertical.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "spacing") { out[0] = m_spacing; return true; }
        if (name == "thickness") { out[0] = m_thickness; return true; }
        if (name == "intensity") { out[0] = m_intensity; return true; }
        if (name == "vertical") { out[0] = m_vertical ? 1.0f : 0.0f; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "spacing") { m_spacing = static_cast<int>(value[0]); return true; }
        if (name == "thickness") { m_thickness = value[0]; return true; }
        if (name == "intensity") { m_intensity = value[0]; return true; }
        if (name == "vertical") { m_vertical = value[0] > 0.5f; return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    Param<int> m_spacing{"spacing", 2, 1, 20};
    Param<float> m_thickness{"thickness", 0.5f, 0.0f, 1.0f};
    Param<float> m_intensity{"intensity", 0.3f, 0.0f, 1.0f};
    Param<bool> m_vertical{"vertical", false};

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
