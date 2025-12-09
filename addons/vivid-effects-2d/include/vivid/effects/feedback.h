#pragma once

/**
 * @file feedback.h
 * @brief Temporal feedback effect operator
 *
 * Creates feedback trails by blending current frame with previous frame.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Temporal feedback effect
 *
 * Blends the current frame with a transformed version of the previous frame
 * to create motion trails, echo effects, and recursive visual patterns.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | decay | float | 0-1 | 0.95 | How much of previous frame remains |
 * | mix | float | 0-1 | 0.5 | Blend between input and feedback |
 * | offset | vec2 | -100 to 100 | (0,0) | Pixel offset per frame |
 * | zoom | float | 0.5-2 | 1.0 | Scale factor per frame |
 * | rotate | float | -0.1 to 0.1 | 0.0 | Rotation per frame (radians) |
 *
 * @par Example
 * @code
 * chain.add<Feedback>("trails")
 *     .input("source")
 *     .decay(0.92f)
 *     .zoom(1.01f)
 *     .rotate(0.02f);
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Texture with feedback trails
 */
class Feedback : public TextureOperator {
public:
    Feedback() = default;
    ~Feedback() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set input texture
     * @param op Source operator
     * @return Reference for chaining
     */
    Feedback& input(TextureOperator* op) { setInput(0, op); return *this; }

    /**
     * @brief Set decay rate
     * @param d Decay (0-1, default 0.95). Higher = longer trails
     * @return Reference for chaining
     */
    Feedback& decay(float d) { m_decay = d; return *this; }

    /**
     * @brief Set mix ratio
     * @param m Mix (0-1, default 0.5). 0 = input only, 1 = feedback only
     * @return Reference for chaining
     */
    Feedback& mix(float m) { m_mix = m; return *this; }

    /**
     * @brief Set X offset per frame
     * @param x X offset in pixels
     * @return Reference for chaining
     */
    Feedback& offsetX(float x) { m_offset.set(x, m_offset.y()); return *this; }

    /**
     * @brief Set Y offset per frame
     * @param y Y offset in pixels
     * @return Reference for chaining
     */
    Feedback& offsetY(float y) { m_offset.set(m_offset.x(), y); return *this; }

    /**
     * @brief Set zoom factor per frame
     * @param z Zoom (0.5-2, default 1.0). >1 zooms in, <1 zooms out
     * @return Reference for chaining
     */
    Feedback& zoom(float z) { m_zoom = z; return *this; }

    /**
     * @brief Set rotation per frame
     * @param r Rotation in radians (-0.1 to 0.1)
     * @return Reference for chaining
     */
    Feedback& rotate(float r) { m_rotate = r; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Feedback"; }

    std::vector<ParamDecl> params() override {
        return { m_decay.decl(), m_mix.decl(), m_offset.decl(), m_zoom.decl(), m_rotate.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "decay") { out[0] = m_decay; return true; }
        if (name == "mix") { out[0] = m_mix; return true; }
        if (name == "offset") { out[0] = m_offset.x(); out[1] = m_offset.y(); return true; }
        if (name == "zoom") { out[0] = m_zoom; return true; }
        if (name == "rotate") { out[0] = m_rotate; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "decay") { m_decay = value[0]; return true; }
        if (name == "mix") { m_mix = value[0]; return true; }
        if (name == "offset") { m_offset.set(value[0], value[1]); return true; }
        if (name == "zoom") { m_zoom = value[0]; return true; }
        if (name == "rotate") { m_rotate = value[0]; return true; }
        return false;
    }

    /// @}

    // State preservation for hot-reload
    std::unique_ptr<OperatorState> saveState() override;
    void loadState(std::unique_ptr<OperatorState> state) override;

private:
    void createPipeline(Context& ctx);
    void createBufferTexture(Context& ctx);

    // Parameters
    Param<float> m_decay{"decay", 0.95f, 0.0f, 1.0f};
    Param<float> m_mix{"mix", 0.5f, 0.0f, 1.0f};
    Vec2Param m_offset{"offset", 0.0f, 0.0f, -100.0f, 100.0f};
    Param<float> m_zoom{"zoom", 1.0f, 0.5f, 2.0f};
    Param<float> m_rotate{"rotate", 0.0f, -0.1f, 0.1f};

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    // Feedback buffer (previous frame)
    WGPUTexture m_buffer = nullptr;
    WGPUTextureView m_bufferView = nullptr;

    bool m_initialized = false;
    bool m_firstFrame = true;
};

} // namespace vivid::effects
