#pragma once

// Vivid Effects 2D - Displace Operator
// Distorts one texture using another as a displacement map

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

class Displace : public TextureOperator {
public:
    Displace() = default;
    ~Displace() override;

    // Fluent API
    Displace& source(TextureOperator* op) { setInput(0, op); return *this; }
    Displace& map(TextureOperator* op) { setInput(1, op); return *this; }
    Displace& strength(float s) { m_strength = s; return *this; }
    Displace& strengthX(float s) { m_strengthX = s; return *this; }
    Displace& strengthY(float s) { m_strengthY = s; return *this; }

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Displace"; }

private:
    void createPipeline(Context& ctx);

    float m_strength = 0.1f;    // Overall displacement strength
    float m_strengthX = 1.0f;   // X-axis multiplier
    float m_strengthY = 1.0f;   // Y-axis multiplier

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
