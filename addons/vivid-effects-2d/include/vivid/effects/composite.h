#pragma once

/**
 * @file composite.h
 * @brief Blend two textures together
 *
 * Composites two input textures using various blend modes.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Blend modes for compositing
 */
enum class BlendMode {
    Over,       ///< Normal alpha compositing (A over B)
    Add,        ///< Additive blending (A + B)
    Multiply,   ///< Multiply (A * B) - darkens
    Screen,     ///< Screen (1 - (1-A)(1-B)) - lightens
    Overlay,    ///< Overlay - combines multiply and screen
    Difference  ///< Absolute difference |A - B|
};

/**
 * @brief Blend two textures together
 *
 * Composites two input textures using various blend modes with
 * adjustable opacity.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | opacity | float | 0-1 | 1.0 | Blend opacity |
 *
 * @par Example
 * @code
 * chain->add<Image>("photo").path("assets/photo.jpg");
 * chain->add<Noise>("noise").scale(4.0f);
 * chain->add<Composite>("blend")
 *     .inputA("photo")
 *     .inputB("noise")
 *     .mode(BlendMode::Overlay)
 *     .opacity(0.5f);
 * @endcode
 *
 * @par Inputs
 * - inputA: Background texture
 * - inputB: Foreground texture to blend
 *
 * @par Output
 * Blended texture
 */
class Composite : public TextureOperator {
public:
    Composite() = default;
    ~Composite() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set blend mode
     * @param m Blend mode (Over, Add, Multiply, Screen, Overlay, Difference)
     * @return Reference for chaining
     */
    Composite& mode(BlendMode m) { m_mode = m; return *this; }

    /**
     * @brief Set blend opacity
     * @param o Opacity (0-1, default 1.0)
     * @return Reference for chaining
     */
    Composite& opacity(float o) { m_opacity = o; return *this; }

    /**
     * @brief Set background input (A)
     * @param op Background operator
     * @return Reference for chaining
     */
    Composite& inputA(TextureOperator* op) { setInput(0, op); return *this; }

    /**
     * @brief Set foreground input (B)
     * @param op Foreground operator
     * @return Reference for chaining
     */
    Composite& inputB(TextureOperator* op) { setInput(1, op); return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Composite"; }

    std::vector<ParamDecl> params() override {
        return { m_opacity.decl() };
    }
    bool getParam(const std::string& name, float out[4]) override {
        if (name == "opacity") { out[0] = m_opacity; return true; }
        return false;
    }
    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "opacity") { m_opacity = value[0]; return true; }
        return false;
    }

    /// @}

    /**
     * @brief Get blend mode display name
     * @param m Blend mode
     * @return Human-readable name
     */
    static const char* modeName(BlendMode m) {
        switch (m) {
            case BlendMode::Over: return "Over";
            case BlendMode::Add: return "Add";
            case BlendMode::Multiply: return "Multiply";
            case BlendMode::Screen: return "Screen";
            case BlendMode::Overlay: return "Overlay";
            case BlendMode::Difference: return "Difference";
            default: return "Unknown";
        }
    }

private:
    void createPipeline(Context& ctx);
    void updateBindGroup(Context& ctx);

    BlendMode m_mode = BlendMode::Over;
    Param<float> m_opacity{"opacity", 1.0f, 0.0f, 1.0f};

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    WGPUTextureView m_lastInputA = nullptr;
    WGPUTextureView m_lastInputB = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
