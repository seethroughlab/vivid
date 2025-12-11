#pragma once

/**
 * @file composite.h
 * @brief Blend multiple textures together
 *
 * Composites multiple input textures using various blend modes.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>
#include <array>

namespace vivid::effects {

/// Maximum number of inputs for Composite operator
static constexpr int COMPOSITE_MAX_INPUTS = 8;

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
 * @brief Blend multiple textures together
 *
 * Composites up to 8 input textures using various blend modes.
 * Layers are blended sequentially: result = blend(blend(in0, in1), in2)...
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | opacity | float | 0-1 | 1.0 | Blend opacity for all layers |
 *
 * @par Example
 * @code
 * // Multi-layer compositing
 * chain.add<Composite>("comp")
 *     .input(0, &background)
 *     .input(1, &layer1)
 *     .input(2, &layer2)
 *     .input(3, &layer3)
 *     .mode(BlendMode::Over)
 *     .opacity(1.0f);
 *
 * // Legacy 2-input API still works
 * chain.add<Composite>("blend")
 *     .inputA(&photo)
 *     .inputB(&noise)
 *     .mode(BlendMode::Overlay);
 * @endcode
 *
 * @par Inputs
 * - input(0): Base/background texture
 * - input(1..7): Layers blended on top sequentially
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
     * @brief Set input at specific index
     * @param index Input index (0 = base, 1-7 = layers)
     * @param op Texture operator
     * @return Reference for chaining
     */
    Composite& input(int index, TextureOperator* op) {
        if (index >= 0 && index < COMPOSITE_MAX_INPUTS) {
            setInput(index, op);
            if (index >= m_inputCount) {
                m_inputCount = index + 1;
            }
        }
        return *this;
    }

    /**
     * @brief Set background input (legacy API, same as input(0, op))
     * @param op Background operator
     * @return Reference for chaining
     */
    Composite& inputA(TextureOperator* op) { return input(0, op); }

    /**
     * @brief Set foreground input (legacy API, same as input(1, op))
     * @param op Foreground operator
     * @return Reference for chaining
     */
    Composite& inputB(TextureOperator* op) { return input(1, op); }

    /**
     * @brief Get number of active inputs
     */
    int inputCount() const { return m_inputCount; }

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
    void createDummyTexture(Context& ctx);

    BlendMode m_mode = BlendMode::Over;
    Param<float> m_opacity{"opacity", 1.0f, 0.0f, 1.0f};
    int m_inputCount = 0;

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    // Dummy texture for unused input slots
    WGPUTexture m_dummyTexture = nullptr;
    WGPUTextureView m_dummyView = nullptr;

    // Cache last input views to detect changes
    std::array<WGPUTextureView, COMPOSITE_MAX_INPUTS> m_lastInputViews = {};
    int m_lastInputCount = 0;

    bool m_initialized = false;
};

} // namespace vivid::effects
