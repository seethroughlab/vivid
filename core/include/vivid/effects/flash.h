// Vivid - Flash Operator
// Beat-synced flash/strobe effect with configurable decay and color

#pragma once

#include <vivid/effects/simple_texture_effect.h>
#include <vivid/param.h>

namespace vivid::effects {

/// @brief Uniform buffer for Flash effect
struct FlashUniforms {
    float intensity;
    float mode;
    float pad0, pad1;
    float color[4];  // RGB + padding
};

/**
 * @brief Flash overlay effect for beat-synced visuals
 *
 * Creates a flash that triggers instantly and decays over time.
 * Perfect for kick-triggered strobes, snare flashes, or any
 * rhythmic visual accents.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | decay | float | 0.5-0.995 | 0.92 | Decay rate per frame |
 * | color | vec3 | 0-1 | (1,1,1) | Flash color RGB |
 * | mode | int | 0-2 | 0 | 0=Additive, 1=Screen, 2=Replace |
 *
 * @par Example
 * @code
 * auto& flash = chain.add<Flash>("flash");
 * flash.input(&video);
 * flash.decay = 0.9f;      // Fast decay
 * flash.color = {1, 0.9, 0.8}; // Warm white flash
 *
 * // In update():
 * if (beat.triggered()) {
 *     flash.trigger();
 * }
 * @endcode
 */
class Flash : public SimpleTextureEffect<Flash, FlashUniforms> {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    /// Decay rate per frame (0.8 = fast, 0.99 = slow trails)
    Param<float> decay{"decay", 0.92f, 0.5f, 0.995f};

    /// Flash color (RGB, 0-1)
    ColorParam color{"color", 1.0f, 1.0f, 1.0f};

    /// Blend mode: 0 = Additive, 1 = Screen, 2 = Replace
    Param<int> mode{"mode", 0, 0, 2};

    /// @}
    // -------------------------------------------------------------------------

    Flash() {
        registerParam(decay);
        registerParam(color);
        registerParam(mode);
    }

    /// @brief Set input texture
    void input(TextureOperator* op) { setInput(0, op); }

    // --- Triggering ---

    /// Trigger a flash (sets intensity to 1.0)
    void trigger() { m_intensity = 1.0f; }

    /// Trigger with custom intensity (0-1)
    void trigger(float intensity) { m_intensity = intensity; }

    /// Current flash intensity (0-1, decays over time)
    float intensity() const { return m_intensity; }

    /// @brief Get uniform values for GPU
    FlashUniforms getUniforms() {
        // Apply decay each frame
        m_intensity *= static_cast<float>(decay);
        if (m_intensity < 0.001f) m_intensity = 0.0f;

        return {
            m_intensity,
            static_cast<float>(static_cast<int>(mode)),
            0.0f, 0.0f,
            {color.r(), color.g(), color.b(), 1.0f}
        };
    }

    std::string name() const override { return "Flash"; }

    /// @brief Fragment shader source (used by CRTP base)
    const char* fragmentShader() const override;

private:
    float m_intensity = 0.0f;
};

#ifdef _WIN32
extern template class SimpleTextureEffect<Flash, FlashUniforms>;
#endif

} // namespace vivid::effects
