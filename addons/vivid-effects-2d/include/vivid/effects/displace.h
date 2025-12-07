#pragma once

// Vivid Effects 2D - Displace Operator
// Distorts one texture using another as a displacement map

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

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

    std::vector<ParamDecl> params() override {
        return { m_strength.decl(), m_strengthX.decl(), m_strengthY.decl() };
    }

private:
    void createPipeline(Context& ctx);

    Param<float> m_strength{"strength", 0.1f, 0.0f, 1.0f};
    Param<float> m_strengthX{"strengthX", 1.0f, 0.0f, 2.0f};
    Param<float> m_strengthY{"strengthY", 1.0f, 0.0f, 2.0f};

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
