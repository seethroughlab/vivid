#pragma once

// Vivid Effects 2D - Blur Operator
// Separable Gaussian blur with configurable radius and passes

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

class Blur : public TextureOperator {
public:
    Blur() = default;
    ~Blur() override;

    // Fluent API
    Blur& input(TextureOperator* op) { setInput(0, op); return *this; }
    Blur& radius(float r) { m_radius = r; return *this; }
    Blur& passes(int p) { m_passes = p; return *this; }

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Blur"; }

private:
    void createPipeline(Context& ctx);

    float m_radius = 5.0f;
    int m_passes = 1;

    // GPU resources
    WGPURenderPipeline m_pipelineH = nullptr;  // Horizontal pass
    WGPURenderPipeline m_pipelineV = nullptr;  // Vertical pass
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    // Ping-pong textures for multi-pass
    WGPUTexture m_tempTexture = nullptr;
    WGPUTextureView m_tempView = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
