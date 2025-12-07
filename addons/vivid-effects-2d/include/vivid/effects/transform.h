#pragma once

// Vivid Effects 2D - Transform Operator
// Scale, rotate, and translate textures

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

class Transform : public TextureOperator {
public:
    Transform() = default;
    ~Transform() override;

    // Fluent API
    Transform& input(TextureOperator* op) { setInput(0, op); return *this; }
    Transform& scale(float s) { m_scaleX = s; m_scaleY = s; return *this; }
    Transform& scale(float x, float y) { m_scaleX = x; m_scaleY = y; return *this; }
    Transform& rotate(float radians) { m_rotation = radians; return *this; }
    Transform& translate(float x, float y) { m_translateX = x; m_translateY = y; return *this; }
    Transform& pivot(float x, float y) { m_pivotX = x; m_pivotY = y; return *this; }

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Transform"; }

private:
    void createPipeline(Context& ctx);

    float m_scaleX = 1.0f;
    float m_scaleY = 1.0f;
    float m_rotation = 0.0f;
    float m_translateX = 0.0f;
    float m_translateY = 0.0f;
    float m_pivotX = 0.5f;  // Center pivot by default
    float m_pivotY = 0.5f;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
