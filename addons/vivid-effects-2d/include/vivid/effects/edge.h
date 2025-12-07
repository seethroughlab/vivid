#pragma once

// Vivid Effects 2D - Edge Operator
// Sobel edge detection

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

class Edge : public TextureOperator {
public:
    Edge() = default;
    ~Edge() override;

    // Fluent API
    Edge& input(TextureOperator* op) { setInput(0, op); return *this; }
    Edge& strength(float s) { m_strength = s; return *this; }
    Edge& threshold(float t) { m_threshold = t; return *this; }
    Edge& invert(bool i) { m_invert = i; return *this; }

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Edge"; }

private:
    void createPipeline(Context& ctx);

    float m_strength = 1.0f;
    float m_threshold = 0.0f;
    bool m_invert = false;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
