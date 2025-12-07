#pragma once

// Vivid Effects 2D - Feedback Operator
// Creates feedback trails by blending current frame with previous frame

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

class Feedback : public TextureOperator {
public:
    Feedback() = default;
    ~Feedback() override;

    // Fluent API
    Feedback& input(TextureOperator* op) { setInput(0, op); return *this; }
    Feedback& decay(float d) { m_decay = d; return *this; }      // 0-1, how much previous frame remains
    Feedback& mix(float m) { m_mix = m; return *this; }          // 0-1, blend between input and feedback
    Feedback& offsetX(float x) { m_offsetX = x; return *this; }  // Pixel offset for motion
    Feedback& offsetY(float y) { m_offsetY = y; return *this; }
    Feedback& zoom(float z) { m_zoom = z; return *this; }        // 1.0 = no zoom, >1 = zoom in
    Feedback& rotate(float r) { m_rotate = r; return *this; }    // Rotation in radians per frame

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Feedback"; }

    // State preservation for hot-reload
    std::unique_ptr<OperatorState> saveState() override;
    void loadState(std::unique_ptr<OperatorState> state) override;

private:
    void createPipeline(Context& ctx);
    void createBufferTexture(Context& ctx);

    // Parameters
    float m_decay = 0.95f;
    float m_mix = 0.5f;
    float m_offsetX = 0.0f;
    float m_offsetY = 0.0f;
    float m_zoom = 1.0f;
    float m_rotate = 0.0f;

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
