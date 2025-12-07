#pragma once

// Vivid Effects 2D - Gradient Operator
// Generates gradient patterns: linear, radial, angular, diamond

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

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
    Gradient& angle(float a) { m_angle = a; return *this; }
    Gradient& center(float x, float y) { m_center.set(x, y); return *this; }
    Gradient& scale(float s) { m_scale = s; return *this; }
    Gradient& offset(float o) { m_offset = o; return *this; }
    Gradient& colorA(float r, float g, float b, float a = 1.0f) {
        m_colorA.set(r, g, b, a);
        return *this;
    }
    Gradient& colorB(float r, float g, float b, float a = 1.0f) {
        m_colorB.set(r, g, b, a);
        return *this;
    }

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Gradient"; }

    std::vector<ParamDecl> params() override {
        return {
            m_angle.decl(), m_scale.decl(), m_offset.decl(),
            m_center.decl(), m_colorA.decl(), m_colorB.decl()
        };
    }

private:
    void createPipeline(Context& ctx);

    GradientMode m_mode = GradientMode::Linear;
    Param<float> m_angle{"angle", 0.0f, 0.0f, 6.283f};
    Param<float> m_scale{"scale", 1.0f, 0.1f, 10.0f};
    Param<float> m_offset{"offset", 0.0f, -1.0f, 1.0f};
    Vec2Param m_center{"center", 0.5f, 0.5f, 0.0f, 1.0f};
    ColorParam m_colorA{"colorA", 0.0f, 0.0f, 0.0f, 1.0f};
    ColorParam m_colorB{"colorB", 1.0f, 1.0f, 1.0f, 1.0f};

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
