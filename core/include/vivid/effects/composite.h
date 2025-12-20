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
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> opacity{"opacity", 1.0f, 0.0f, 1.0f}; ///< Blend opacity for all layers

    /// @}
    // -------------------------------------------------------------------------

    Composite() {
        registerParam(opacity);
    }
    ~Composite() override;

    /// @brief Set blend mode (Over, Add, Multiply, Screen, Overlay, Difference)
    void mode(BlendMode m) {
        if (m_mode != m) { m_mode = m; markDirty(); }
    }

    /// @brief Set input at specific index (0 = base, 1-7 = layers)
    void input(int index, TextureOperator* op) {
        if (index >= 0 && index < COMPOSITE_MAX_INPUTS) {
            setInput(index, op);
            if (index >= m_inputCount) {
                m_inputCount = index + 1;
            }
        }
    }

    /// @brief Set background input (legacy API, same as input(0, op))
    void inputA(TextureOperator* op) { input(0, op); }

    /// @brief Set foreground input (legacy API, same as input(1, op))
    void inputB(TextureOperator* op) { input(1, op); }

    /// @brief Get number of active inputs
    int inputCount() const { return m_inputCount; }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Composite"; }

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
};

} // namespace vivid::effects
