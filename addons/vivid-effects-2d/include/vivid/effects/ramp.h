#pragma once

// Vivid Effects 2D - Ramp Operator
// Generates animated color gradients with HSV support

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

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
    Ramp& offset(float x, float y) { m_offset.set(x, y); return *this; }
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

    std::vector<ParamDecl> params() override {
        return {
            m_angle.decl(), m_scale.decl(), m_repeat.decl(),
            m_hueOffset.decl(), m_hueSpeed.decl(), m_hueRange.decl(),
            m_saturation.decl(), m_brightness.decl(), m_offset.decl()
        };
    }

private:
    void createPipeline(Context& ctx);

    // Parameters
    RampType m_type = RampType::Linear;
    Param<float> m_angle{"angle", 0.0f, 0.0f, 6.283f};
    Param<float> m_scale{"scale", 1.0f, 0.1f, 10.0f};
    Param<float> m_repeat{"repeat", 1.0f, 1.0f, 10.0f};
    Vec2Param m_offset{"offset", 0.0f, 0.0f};

    // HSV parameters
    Param<float> m_hueOffset{"hueOffset", 0.0f, 0.0f, 1.0f};
    Param<float> m_hueSpeed{"hueSpeed", 0.5f, 0.0f, 2.0f};
    Param<float> m_hueRange{"hueRange", 1.0f, 0.0f, 1.0f};
    Param<float> m_saturation{"saturation", 1.0f, 0.0f, 1.0f};
    Param<float> m_brightness{"brightness", 1.0f, 0.0f, 1.0f};

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
