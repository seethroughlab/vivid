#pragma once

// Vivid Effects 2D - Bloom Operator
// Glow effect with threshold, blur, and blend

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

class Bloom : public TextureOperator {
public:
    Bloom() = default;
    ~Bloom() override;

    // Fluent API
    Bloom& input(TextureOperator* op) { setInput(0, op); return *this; }
    Bloom& threshold(float t) { m_threshold = t; return *this; }  // Brightness cutoff
    Bloom& intensity(float i) { m_intensity = i; return *this; }  // Bloom strength
    Bloom& radius(float r) { m_radius = r; return *this; }        // Blur radius
    Bloom& passes(int p) { m_passes = p; return *this; }          // Blur iterations

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Bloom"; }

private:
    void createPipeline(Context& ctx);

    float m_threshold = 0.8f;
    float m_intensity = 1.0f;
    float m_radius = 10.0f;
    int m_passes = 2;

    // GPU resources - need multiple passes
    WGPURenderPipeline m_thresholdPipeline = nullptr;
    WGPURenderPipeline m_blurHPipeline = nullptr;
    WGPURenderPipeline m_blurVPipeline = nullptr;
    WGPURenderPipeline m_combinePipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    // Intermediate textures
    WGPUTexture m_brightTexture = nullptr;
    WGPUTextureView m_brightView = nullptr;
    WGPUTexture m_blurTexture = nullptr;
    WGPUTextureView m_blurView = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
