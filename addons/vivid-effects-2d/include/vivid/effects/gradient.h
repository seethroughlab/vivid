#pragma once

// Vivid Effects 2D - Gradient Operator
// Generates gradient patterns: linear, radial, angular, diamond

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

enum class GradientMode {
    Linear,    // Left to right (or by angle)
    Radial,    // Center outward
    Angular,   // Around center
    Diamond    // Diamond pattern from center
};

class Gradient : public TextureOperator {
public:
    Gradient() = default;
    ~Gradient() override;

    // Fluent API
    Gradient& mode(GradientMode m) { m_mode = m; return *this; }
    Gradient& angle(float a) { m_angle = a; return *this; }          // Rotation in radians
    Gradient& centerX(float x) { m_centerX = x; return *this; }      // 0-1, center position
    Gradient& centerY(float y) { m_centerY = y; return *this; }
    Gradient& scale(float s) { m_scale = s; return *this; }          // Gradient scale
    Gradient& offset(float o) { m_offset = o; return *this; }        // Shift gradient position
    Gradient& colorA(float r, float g, float b, float a = 1.0f) {
        m_colorA[0] = r; m_colorA[1] = g; m_colorA[2] = b; m_colorA[3] = a;
        return *this;
    }
    Gradient& colorB(float r, float g, float b, float a = 1.0f) {
        m_colorB[0] = r; m_colorB[1] = g; m_colorB[2] = b; m_colorB[3] = a;
        return *this;
    }

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Gradient"; }

private:
    void createPipeline(Context& ctx);

    GradientMode m_mode = GradientMode::Linear;
    float m_angle = 0.0f;
    float m_centerX = 0.5f;
    float m_centerY = 0.5f;
    float m_scale = 1.0f;
    float m_offset = 0.0f;
    float m_colorA[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float m_colorB[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
