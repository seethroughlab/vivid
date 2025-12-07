#pragma once

// Vivid Effects 2D - Ramp Operator
// Generates animated color gradients with HSV support

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

enum class RampType {
    Linear,      // Left to right
    Radial,      // Center outward
    Angular,     // Circular sweep
    Diamond      // Diamond pattern
};

class Ramp : public TextureOperator {
public:
    Ramp() = default;
    ~Ramp() override;

    // Fluent API
    Ramp& type(RampType t) { m_type = t; return *this; }
    Ramp& angle(float a) { m_angle = a; return *this; }
    Ramp& offset(float x, float y) { m_offsetX = x; m_offsetY = y; return *this; }
    Ramp& scale(float s) { m_scale = s; return *this; }
    Ramp& repeat(float r) { m_repeat = r; return *this; }

    // HSV animation
    Ramp& hueOffset(float h) { m_hueOffset = h; return *this; }
    Ramp& hueSpeed(float s) { m_hueSpeed = s; return *this; }
    Ramp& hueRange(float r) { m_hueRange = r; return *this; }
    Ramp& saturation(float s) { m_saturation = s; return *this; }
    Ramp& brightness(float b) { m_brightness = b; return *this; }

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Ramp"; }

private:
    void createPipeline(Context& ctx);

    // Parameters
    RampType m_type = RampType::Linear;
    float m_angle = 0.0f;
    float m_offsetX = 0.0f;
    float m_offsetY = 0.0f;
    float m_scale = 1.0f;
    float m_repeat = 1.0f;

    // HSV parameters
    float m_hueOffset = 0.0f;
    float m_hueSpeed = 0.5f;
    float m_hueRange = 1.0f;
    float m_saturation = 1.0f;
    float m_brightness = 1.0f;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
