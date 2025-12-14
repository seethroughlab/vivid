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
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> decay{"decay", 0.95f, 0.0f, 1.0f};          ///< How much of previous frame remains
    Param<float> mix{"mix", 0.5f, 0.0f, 1.0f};               ///< Blend between input and feedback
    Vec2Param offset{"offset", 0.0f, 0.0f, -100.0f, 100.0f}; ///< Pixel offset per frame
    Param<float> zoom{"zoom", 1.0f, 0.5f, 2.0f};             ///< Scale factor per frame
    Param<float> rotate{"rotate", 0.0f, -0.1f, 0.1f};        ///< Rotation per frame (radians)

    /// @}
    // -------------------------------------------------------------------------

    Feedback() {
        registerParam(decay);
        registerParam(mix);
        registerParam(offset);
        registerParam(zoom);
        registerParam(rotate);
    }
    ~Feedback() override;

    /// @brief Set input texture
    Feedback& input(TextureOperator* op) { setInput(0, op); return *this; }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Feedback"; }

    /// @}

    // State preservation for hot-reload
    std::unique_ptr<OperatorState> saveState() override;
    void loadState(std::unique_ptr<OperatorState> state) override;

private:
    void createPipeline(Context& ctx);
    void createBufferTexture(Context& ctx);

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
