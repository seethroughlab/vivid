#pragma once

// Vivid Effects 2D - SolidColor Operator
// Generates a solid color or gradient

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

class SolidColor : public TextureOperator {
public:
    SolidColor() = default;
    ~SolidColor() override;

    // Fluent API
    SolidColor& color(float r, float g, float b, float a = 1.0f) {
        m_r = r; m_g = g; m_b = b; m_a = a;
        return *this;
    }

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "SolidColor"; }

private:
    void createPipeline(Context& ctx);

    // Parameters
    float m_r = 0.0f;
    float m_g = 0.0f;
    float m_b = 0.0f;
    float m_a = 1.0f;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
