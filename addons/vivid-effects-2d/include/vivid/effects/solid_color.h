#pragma once

/**
 * @file solid_color.h
 * @brief Solid color generator
 *
 * Generates a uniform solid color texture.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Solid color generator
 *
 * Generates a texture filled with a single uniform color.
 * Useful as a background or for masking operations.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | color | color | - | black | Fill color (RGBA) |
 *
 * @par Example
 * @code
 * chain.add<SolidColor>("bg")
 *     .color(0.1f, 0.1f, 0.2f);  // Dark blue background
 * @endcode
 *
 * @par Inputs
 * None (generator)
 *
 * @par Output
 * Solid color texture
 */
class SolidColor : public TextureOperator {
public:
    SolidColor() = default;
    ~SolidColor() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set fill color
     * @param r Red (0-1)
     * @param g Green (0-1)
     * @param b Blue (0-1)
     * @param a Alpha (0-1, default 1.0)
     * @return Reference for chaining
     */
    SolidColor& color(float r, float g, float b, float a = 1.0f) {
        if (m_color.r() != r || m_color.g() != g || m_color.b() != b || m_color.a() != a) {
            m_color.set(r, g, b, a);
            markDirty();
        }
        return *this;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "SolidColor"; }

    std::vector<ParamDecl> params() override {
        return { m_color.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "color") { out[0] = m_color.r(); out[1] = m_color.g(); out[2] = m_color.b(); out[3] = m_color.a(); return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "color") { color(value[0], value[1], value[2], value[3]); return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    // Parameters
    ColorParam m_color{"color", 0.0f, 0.0f, 0.0f, 1.0f};

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
